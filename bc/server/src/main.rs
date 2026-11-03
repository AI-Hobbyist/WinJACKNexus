// SPDX-License-Identifier: AGPL-3.0-only
mod listen;
mod ogg;
mod registry;
mod transport;
mod write_timeout;
use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        Query, State,
    },
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    routing::get,
    Json, Router,
};
use rand::RngCore;
use serde_json::json;
use std::{
    collections::BTreeMap,
    error::Error,
    io::Write,
    net::SocketAddr,
    sync::{Arc, Mutex},
    time::Duration,
};
use subtle::ConstantTimeEq;
use tokio::net::{TcpListener, UdpSocket};

struct App {
    token: String,
    udp_port: u16,
    registry: Mutex<registry::Registry>,
}

fn failure(status: StatusCode, code: &str) -> Response {
    (status, Json(json!({"code": code}))).into_response()
}

async fn publish(
    State(app): State<Arc<App>>,
    headers: HeaderMap,
    Query(query): Query<BTreeMap<String, String>>,
    upgrade: Result<WebSocketUpgrade, axum::extract::ws::rejection::WebSocketUpgradeRejection>,
) -> Response {
    let authorization = headers
        .get("authorization")
        .and_then(|value| value.to_str().ok());
    let expected = format!("Bearer {}", app.token);
    if !bool::from(
        authorization
            .unwrap_or("")
            .as_bytes()
            .ct_eq(expected.as_bytes()),
    ) {
        let mut response = failure(StatusCode::UNAUTHORIZED, "unauthorized");
        response
            .headers_mut()
            .insert("www-authenticate", "Bearer".parse().unwrap());
        return response;
    }
    let Some(publisher) = query.get("publisher").filter(|value| valid_id(value)) else {
        return failure(StatusCode::BAD_REQUEST, "invalid_publisher");
    };
    let Ok(upgrade) = upgrade else {
        return failure(StatusCode::BAD_REQUEST, "websocket_required");
    };
    let mut session = [0; 16];
    let mut key = [0; 32];
    rand::rngs::OsRng.fill_bytes(&mut session);
    rand::rngs::OsRng.fill_bytes(&mut key);
    {
        let mut registry = app.registry.lock().unwrap();
        if registry
            .sessions
            .values()
            .any(|session| &session.publisher == publisher)
        {
            return failure(StatusCode::CONFLICT, "publisher_occupied");
        }
        if registry.sessions.len() >= 64 {
            return failure(StatusCode::TOO_MANY_REQUESTS, "session_capacity");
        }
        registry
            .sessions
            .insert(session, registry::Session::new(publisher.clone(), key));
    }
    let publisher = publisher.clone();
    upgrade
        .max_message_size(65536)
        .on_upgrade(move |socket| control(socket, app, publisher, session, key))
        .into_response()
}

fn valid_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-' || byte == b'_')
}

async fn control(
    mut socket: WebSocket,
    app: Arc<App>,
    _publisher: String,
    session: [u8; 16],
    key: [u8; 32],
) {
    let hello = json!({"type": "hello", "version": 1, "session": hex::encode(session),
        "key": hex::encode(key), "udpPort": app.udp_port, "maxDatagram": 1200});
    if socket
        .send(Message::Text(hello.to_string().into()))
        .await
        .is_ok()
    {
        let mut heartbeat = tokio::time::interval(Duration::from_secs(10));
        let mut last_activity = tokio::time::Instant::now();
        loop {
            tokio::select! {
                _ = heartbeat.tick() => {
                    if last_activity.elapsed() >= Duration::from_secs(30) || socket.send(Message::Ping(vec![].into())).await.is_err() {
                        break;
                    }
                }
                message = socket.recv() => {
                    match message {
                        Some(Ok(Message::Close(_))) | None | Some(Err(_)) => break,
                        Some(Ok(Message::Text(text))) => {
                            last_activity = tokio::time::Instant::now();
                            let reply = control_message(&text, &app, &session);
                            if socket.send(Message::Text(reply.to_string().into())).await.is_err() { break; }
                        }
                        Some(Ok(_)) => last_activity = tokio::time::Instant::now(),
                    }
                }
            }
        }
    }
    app.registry.lock().unwrap().remove_session(&session);
}

fn control_message(text: &str, app: &App, session_id: &[u8; 16]) -> serde_json::Value {
    let parsed: Result<serde_json::Value, _> = serde_json::from_str(text);
    let Ok(message) = parsed else {
        return json!({"type": "error", "code": "invalid_json"});
    };
    let request_id = message
        .get("requestId")
        .and_then(|id| id.as_u64())
        .filter(|id| *id <= u32::MAX as u64);
    let Some(request_id) = request_id else {
        return json!({"type": "error", "code": "invalid_request_id"});
    };
    let mut registry = app.registry.lock().unwrap();
    let published_identity = registry
        .sessions
        .get(session_id)
        .map(|session| session.publisher.clone());
    let result = if let Some(session) = registry.sessions.get_mut(session_id) {
        match message.get("type").and_then(|kind| kind.as_str()) {
            Some("register") => serde_json::from_value::<registry::Registration>(message.clone())
                .map_err(|_| "invalid_registration")
                .and_then(|spec| session.register(spec)),
            Some("unregister") => {
                let handle = message
                    .get("handle")
                    .and_then(|value| value.as_u64())
                    .and_then(|value| u32::try_from(value).ok());
                match handle.and_then(|handle| session.streams.remove(&handle)) {
                    Some(_) => Ok(()),
                    None => Err("unknown_handle"),
                }
            }
            _ => Err("unknown_message"),
        }
    } else {
        Err("session_expired")
    };
    if result.is_ok() && message.get("type").and_then(|kind| kind.as_str()) == Some("register") {
        if let (Some(publisher), Some(stream)) = (
            published_identity,
            message.get("streamId").and_then(|id| id.as_str()),
        ) {
            registry.offline.remove(&(publisher, stream.to_owned()));
        }
    }
    match result {
        Ok(()) => json!({"type": "ack", "requestId": request_id}),
        Err(code) => json!({"type": "error", "requestId": request_id, "code": code}),
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let mut arguments = std::env::args().skip(1);
    let mut options = BTreeMap::new();
    let mut insecure = false;
    while let Some(name) = arguments.next() {
        if name == "--insecure-loopback" {
            insecure = true;
        } else if matches!(
            name.as_str(),
            "--bind" | "--udp-bind" | "--publish-token-file" | "--tls-cert" | "--tls-key"
        ) {
            let value = arguments.next().ok_or("missing option value")?;
            if options.insert(name, value).is_some() {
                return Err("duplicate option".into());
            }
        } else {
            return Err(format!("unknown option {name}").into());
        }
    }
    let bind: SocketAddr = options
        .get("--bind")
        .map(String::as_str)
        .unwrap_or("0.0.0.0:9443")
        .parse()?;
    let udp_bind: SocketAddr = options
        .get("--udp-bind")
        .map(String::as_str)
        .unwrap_or("0.0.0.0:9444")
        .parse()?;
    let token = std::fs::read_to_string(
        options
            .get("--publish-token-file")
            .ok_or("--publish-token-file required")?,
    )?;
    let token = token.trim().to_owned();
    if token.is_empty() {
        return Err("empty publish token".into());
    }
    if insecure && (!bind.ip().is_loopback() || !udp_bind.ip().is_loopback()) {
        return Err("insecure mode requires loopback HTTP and UDP binds".into());
    }
    let udp = Arc::new(UdpSocket::bind(udp_bind).await?);
    let state = Arc::new(App {
        token,
        udp_port: udp.local_addr()?.port(),
        registry: Mutex::new(registry::Registry::default()),
    });
    tokio::spawn(transport::receive(udp.clone(), state.clone()));
    let app = Router::new()
        .route("/bc/publish", get(publish))
        .route("/bc/listen", get(listen::listen))
        .with_state(state);
    if insecure {
        let listener = TcpListener::bind(bind).await?;
        println!(
            "{}",
            json!({"http": listener.local_addr()?.to_string(), "udp": udp.local_addr()?.to_string()})
        );
        std::io::stdout().flush()?;
        axum_server::from_tcp(listener.into_std()?)?
            .acceptor(write_timeout::WriteTimeout(
                axum_server::accept::DefaultAcceptor::new(),
            ))
            .serve(app.into_make_service())
            .await?;
    } else {
        rustls::crypto::ring::default_provider()
            .install_default()
            .map_err(|_| "TLS provider already installed")?;
        let cert = options
            .get("--tls-cert")
            .ok_or("--tls-cert required outside loopback mode")?;
        let key = options
            .get("--tls-key")
            .ok_or("--tls-key required outside loopback mode")?;
        let tls = axum_server::tls_rustls::RustlsConfig::from_pem_file(cert, key).await?;
        let listener = TcpListener::bind(bind).await?;
        println!(
            "{}",
            json!({"http": listener.local_addr()?.to_string(), "udp": udp.local_addr()?.to_string(), "tls": true})
        );
        std::io::stdout().flush()?;
        axum_server::from_tcp(listener.into_std()?)?
            .acceptor(write_timeout::WriteTimeout(
                axum_server::tls_rustls::RustlsAcceptor::new(tls),
            ))
            .serve(app.into_make_service())
            .await?;
    }
    Ok(())
}
