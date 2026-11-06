// SPDX-License-Identifier: AGPL-3.0-only
use serde::Deserialize;
use std::{collections::BTreeSet, error::Error, net::SocketAddr, path::PathBuf};

#[derive(Default, Deserialize)]
#[serde(deny_unknown_fields)]
struct Values {
    bind: Option<SocketAddr>,
    udp_bind: Option<SocketAddr>,
    publish_token_file: Option<PathBuf>,
    tls_cert: Option<PathBuf>,
    tls_key: Option<PathBuf>,
    insecure_loopback: Option<bool>,
}

pub struct Settings {
    pub bind: SocketAddr,
    pub udp_bind: SocketAddr,
    pub publish_token_file: PathBuf,
    pub tls_cert: Option<PathBuf>,
    pub tls_key: Option<PathBuf>,
    pub insecure_loopback: bool,
}

impl Settings {
    pub fn from_args(arguments: impl IntoIterator<Item = String>) -> Result<Self, Box<dyn Error>> {
        let mut arguments = arguments.into_iter().peekable();
        let mut overrides = Values::default();
        let mut config_path = None;
        let mut seen = BTreeSet::new();
        while let Some(name) = arguments.next() {
            if !seen.insert(name.clone()) {
                return Err(format!("duplicate option {name}").into());
            }
            if name == "--insecure-loopback" {
                overrides.insecure_loopback = Some(match arguments.peek().map(String::as_str) {
                    Some("true" | "false") => arguments.next().unwrap().parse()?,
                    _ => true,
                });
                continue;
            }
            if !matches!(
                name.as_str(),
                "--config"
                    | "--bind"
                    | "--udp-bind"
                    | "--publish-token-file"
                    | "--tls-cert"
                    | "--tls-key"
            ) {
                return Err(format!("unknown option {name}").into());
            }
            let value = arguments
                .next()
                .filter(|value| !value.starts_with("--"))
                .ok_or_else(|| format!("missing value for {name}"))?;
            match name.as_str() {
                "--config" => config_path = Some(PathBuf::from(value)),
                "--bind" => overrides.bind = Some(value.parse()?),
                "--udp-bind" => overrides.udp_bind = Some(value.parse()?),
                "--publish-token-file" => overrides.publish_token_file = Some(value.into()),
                "--tls-cert" => overrides.tls_cert = Some(value.into()),
                "--tls-key" => overrides.tls_key = Some(value.into()),
                _ => unreachable!(),
            }
        }
        let mut values = match config_path {
            Some(path) => {
                let path = std::fs::canonicalize(path)?;
                let mut values: Values = serde_json::from_slice(&std::fs::read(&path)?)?;
                let directory = path.parent().unwrap();
                for file in [
                    &mut values.publish_token_file,
                    &mut values.tls_cert,
                    &mut values.tls_key,
                ] {
                    if let Some(relative) = file.as_ref().filter(|file| file.is_relative()) {
                        *file = Some(directory.join(relative));
                    }
                }
                values
            }
            None => Values::default(),
        };
        values.bind = overrides.bind.or(values.bind);
        values.udp_bind = overrides.udp_bind.or(values.udp_bind);
        values.publish_token_file = overrides.publish_token_file.or(values.publish_token_file);
        values.tls_cert = overrides.tls_cert.or(values.tls_cert);
        values.tls_key = overrides.tls_key.or(values.tls_key);
        values.insecure_loopback = overrides.insecure_loopback.or(values.insecure_loopback);
        let settings = Self {
            bind: values
                .bind
                .unwrap_or_else(|| "0.0.0.0:9443".parse().unwrap()),
            udp_bind: values
                .udp_bind
                .unwrap_or_else(|| "0.0.0.0:9444".parse().unwrap()),
            publish_token_file: values.publish_token_file.ok_or(
                "publish token file required (--publish-token-file or publish_token_file)",
            )?,
            tls_cert: values.tls_cert,
            tls_key: values.tls_key,
            insecure_loopback: values.insecure_loopback.unwrap_or(false),
        };
        if settings.insecure_loopback {
            if !settings.bind.ip().is_loopback() || !settings.udp_bind.ip().is_loopback() {
                return Err("insecure mode requires loopback HTTP and UDP binds".into());
            }
        }
        if settings.tls_cert.is_some() != settings.tls_key.is_some() {
            return Err("TLS certificate and key must be configured together".into());
        }
        Ok(settings)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;

    fn args(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| value.to_string()).collect()
    }

    #[test]
    fn legacy_arguments_keep_secure_defaults() {
        let settings = Settings::from_args(args(&[
            "--publish-token-file",
            "publish.token",
            "--tls-cert",
            "server.pem",
            "--tls-key",
            "server-key.pem",
        ]))
        .unwrap();
        assert_eq!(settings.bind, "0.0.0.0:9443".parse().unwrap());
        assert_eq!(settings.udp_bind, "0.0.0.0:9444".parse().unwrap());
        assert_eq!(settings.publish_token_file, Path::new("publish.token"));
        assert!(!settings.insecure_loopback);
    }

    #[test]
    fn tls_is_optional_but_partial_tls_and_public_development_mode_are_rejected() {
        let settings = Settings::from_args(args(&["--publish-token-file", "token"])).unwrap();
        assert!(settings.tls_cert.is_none() && settings.tls_key.is_none());
        assert!(Settings::from_args(args(&[
            "--publish-token-file",
            "token",
            "--tls-cert",
            "cert.pem"
        ]))
        .is_err());
        assert!(Settings::from_args(args(&[
            "--publish-token-file",
            "token",
            "--insecure-loopback"
        ]))
        .is_err());
        assert!(Settings::from_args(args(&["--wat"])).is_err());
        assert!(Settings::from_args(args(&["--config", "--bind"])).is_err());
    }

    #[test]
    fn config_paths_are_relative_to_config_and_cli_wins() {
        let folder = std::env::temp_dir().join(format!(
            "bc-config-{}-{}",
            std::process::id(),
            rand::random::<u64>()
        ));
        std::fs::create_dir(&folder).unwrap();
        let path = folder.join("server.json");
        std::fs::write(&path, r#"{"bind":"127.0.0.1:11","udp_bind":"127.0.0.1:12","publish_token_file":"secrets/token","tls_cert":"tls/cert.pem","tls_key":"tls/key.pem","insecure_loopback":true}"#).unwrap();
        let settings = Settings::from_args(vec![
            "--config".into(),
            path.to_str().unwrap().into(),
            "--bind".into(),
            "127.0.0.1:0".into(),
            "--insecure-loopback".into(),
            "false".into(),
            "--tls-key".into(),
            "override.pem".into(),
        ])
        .unwrap();
        assert_eq!(settings.bind.port(), 0);
        assert_eq!(settings.udp_bind.port(), 12);
        assert_eq!(
            settings.publish_token_file,
            std::fs::canonicalize(&folder)
                .unwrap()
                .join("secrets/token")
        );
        assert_eq!(settings.tls_key.as_deref(), Some(Path::new("override.pem")));
        assert!(!settings.insecure_loopback);
        std::fs::write(&path, r#"{"unexpected":true}"#).unwrap();
        assert!(
            Settings::from_args(vec!["--config".into(), path.to_str().unwrap().into()]).is_err()
        );
        std::fs::remove_dir_all(folder).unwrap();
    }
}
