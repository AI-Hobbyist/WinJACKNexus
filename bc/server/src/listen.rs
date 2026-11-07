// SPDX-License-Identifier: AGPL-3.0-only
use crate::{failure, registry::Registration, App};
use axum::{
    body::Body,
    extract::{Query, State},
    http::{header, StatusCode},
    response::{IntoResponse, Response},
};
use base64::{engine::general_purpose::STANDARD, Engine};
use bytes::Bytes;
use std::{collections::BTreeMap, convert::Infallible, sync::Arc, time::Duration};

pub async fn listen(
    State(app): State<Arc<App>>,
    Query(parameters): Query<Vec<(String, String)>>,
) -> Response {
    let mut query = BTreeMap::new();
    for (name, value) in parameters {
        if !matches!(name.as_str(), "publisher" | "stream" | "channel" | "format")
            || query.insert(name, value).is_some()
        {
            return failure(StatusCode::BAD_REQUEST, "invalid_query");
        }
    }
    let Some(publisher) = query
        .get("publisher")
        .filter(|value| crate::valid_id(value))
    else {
        return failure(StatusCode::BAD_REQUEST, "invalid_publisher");
    };
    let Some(stream_id) = query.get("stream").filter(|value| crate::valid_id(value)) else {
        return failure(StatusCode::BAD_REQUEST, "invalid_stream");
    };
    let format = query.get("format").map(String::as_str).unwrap_or("opus");
    if !matches!(format, "wav" | "opus") {
        return failure(StatusCode::BAD_REQUEST, "invalid_format");
    }
    let channel = query.get("channel").map(String::as_str).unwrap_or("all");
    let opus = format == "opus";
    let (spec, selected, mut receiver) = {
        let registry = app.registry.lock().unwrap();
        let stream = registry
            .sessions
            .values()
            .filter(|session| &session.publisher == publisher)
            .flat_map(|session| session.streams.values())
            .find(|stream| &stream.spec.stream_id == stream_id);
        let Some(stream) = stream else {
            let status = if registry
                .offline
                .get(&(publisher.clone(), stream_id.clone()))
                .is_some_and(|time| time.elapsed() < Duration::from_secs(600))
            {
                StatusCode::SERVICE_UNAVAILABLE
            } else {
                StatusCode::NOT_FOUND
            };
            return failure(status, "stream_unavailable");
        };
        let selected = if channel == "all" {
            None
        } else {
            match stream.spec.roles().iter().position(|role| *role == channel) {
                Some(index) => Some(index),
                None => return failure(StatusCode::BAD_REQUEST, "invalid_channel"),
            }
        };
        let variant = if stream.spec.channels() == 1 {
            0
        } else {
            selected.map_or(0, |index| index as u8 + 1)
        };
        let key = if opus { (2, variant) } else { (1, 0) };
        (
            stream.spec.clone(),
            selected,
            stream.senders[&key].subscribe(),
        )
    };
    let wave_header = wav_header(&spec, selected);
    let opus_role = selected.map(|index| spec.roles()[index]).unwrap_or("all");
    let opus_head = STANDARD.decode(&spec.opus_heads[opus_role]).unwrap();
    let channels = spec.channels();
    let rate = if opus { 48000 } else { spec.pcm_rate };
    let body = async_stream::stream! {
        let mut ogg = crate::ogg::Writer::new();
        if opus {
            yield Ok::<_, Infallible>(Bytes::from(ogg.page(&opus_head, 0, true)));
            yield Ok(Bytes::from(ogg.page(&crate::ogg::tags(), 0, false)));
        } else {
            yield Ok(Bytes::from(wave_header));
        }
        let mut next_position = None;
        let mut origin = None;
        loop {
            let audio = match tokio::time::timeout(Duration::from_secs(2), receiver.recv()).await {
                Ok(Ok(audio)) => audio,
                _ => break,
            };
            if audio.created.elapsed() > Duration::from_millis(100) { break; }
            if let Some(next) = next_position {
                if audio.position < next { continue; }
                let gap = audio.position - next;
                if gap > rate as u64 / 10 { break; }
                if gap > 0 && !opus {
                    yield Ok(Bytes::from(vec![0; gap as usize * if selected.is_some() { 1 } else { channels } * 2]));
                }
            }
            next_position = Some(audio.position + audio.samples as u64);
            let data = if opus {
                let start = *origin.get_or_insert(audio.position);
                ogg.page(&audio.data, audio.position + audio.samples as u64 - start, false)
            } else { select_pcm(&audio.data, channels, selected) };
            yield Ok(Bytes::from(data));
        }
    };
    let mut response = Body::from_stream(body).into_response();
    let headers = response.headers_mut();
    headers.insert(
        header::CONTENT_TYPE,
        if opus {
            "audio/ogg; codecs=opus"
        } else {
            "audio/wav"
        }
        .parse()
        .unwrap(),
    );
    headers.insert(header::CONTENT_DISPOSITION, "inline".parse().unwrap());
    headers.insert(header::CACHE_CONTROL, "no-store".parse().unwrap());
    headers.insert(header::ACCEPT_RANGES, "none".parse().unwrap());
    headers.insert("x-content-type-options", "nosniff".parse().unwrap());
    response
}

fn select_pcm(data: &[u8], channels: usize, selected: Option<usize>) -> Vec<u8> {
    let order: Vec<usize> = match selected {
        Some(index) => vec![index],
        None if channels == 8 => vec![0, 1, 2, 3, 6, 7, 4, 5],
        None => (0..channels).collect(),
    };
    let mut output = Vec::with_capacity(data.len());
    for frame in data.chunks_exact(channels * 2) {
        for index in &order {
            output.extend_from_slice(&frame[index * 2..index * 2 + 2]);
        }
    }
    output
}

fn wav_header(spec: &Registration, selected: Option<usize>) -> Vec<u8> {
    let channels = if selected.is_some() {
        1
    } else {
        spec.channels()
    } as u16;
    let extended = channels > 2;
    let mut output = Vec::new();
    output.extend_from_slice(b"RIFF\xff\xff\xff\xffWAVEfmt ");
    output.extend_from_slice(&(if extended { 40u32 } else { 16u32 }).to_le_bytes());
    output.extend_from_slice(&(if extended { 0xfffeu16 } else { 1u16 }).to_le_bytes());
    output.extend_from_slice(&channels.to_le_bytes());
    output.extend_from_slice(&spec.pcm_rate.to_le_bytes());
    output.extend_from_slice(&(spec.pcm_rate * channels as u32 * 2).to_le_bytes());
    output.extend_from_slice(&(channels * 2).to_le_bytes());
    output.extend_from_slice(&16u16.to_le_bytes());
    if extended {
        output.extend_from_slice(&22u16.to_le_bytes());
        output.extend_from_slice(&16u16.to_le_bytes());
        output.extend_from_slice(&(if channels == 6 { 0x60fu32 } else { 0x63fu32 }).to_le_bytes());
        output.extend_from_slice(&[1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113]);
    }
    output.extend_from_slice(b"data\xff\xff\xff\xff");
    output
}
