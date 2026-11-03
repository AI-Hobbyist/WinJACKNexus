// SPDX-License-Identifier: AGPL-3.0-only
use crate::transport::{Assembly, Audio};
use base64::{engine::general_purpose::STANDARD, Engine};
use serde::Deserialize;
use std::{
    collections::{BTreeMap, BTreeSet, HashMap},
    sync::Arc,
    time::{Duration, Instant},
};
use tokio::sync::broadcast;

pub const ROLES: [&str; 8] = ["L", "R", "C", "LFE", "Ls", "Rs", "Lrb", "Rrb"];

#[derive(Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Registration {
    pub stream_id: String,
    pub handle: u32,
    pub epoch: u32,
    pub display_name: String,
    pub category: Vec<String>,
    pub layout: String,
    pub pcm_rate: u32,
    pub opus_heads: BTreeMap<String, String>,
}

impl Registration {
    pub fn channels(&self) -> usize {
        match self.layout.as_str() {
            "mono" => 1,
            "stereo" => 2,
            "5.1" => 6,
            "7.1" => 8,
            _ => 0,
        }
    }

    pub fn roles(&self) -> &[&str] {
        if self.channels() == 1 {
            &["M"]
        } else {
            &ROLES[..self.channels()]
        }
    }

    pub fn valid(&self) -> bool {
        let channels = self.channels();
        if channels == 0
            || !crate::valid_id(&self.stream_id)
            || self.handle == 0
            || self.epoch == 0
            || self.display_name.is_empty()
            || self.display_name.len() > 1024
            || self.category.len() > 32
            || self
                .category
                .iter()
                .any(|name| name.is_empty() || name.len() > 1024)
            || !(8000..=192000).contains(&self.pcm_rate)
        {
            return false;
        }
        std::iter::once(("all", channels))
            .chain(self.roles().iter().map(|role| (*role, 1)))
            .all(|(name, count)| {
                self.opus_heads
                    .get(name)
                    .and_then(|head| STANDARD.decode(head).ok())
                    .is_some_and(|head| {
                        if head.len() < 19
                            || &head[..8] != b"OpusHead"
                            || head[8] != 1
                            || head[9] as usize != count
                        {
                            return false;
                        }
                        if count <= 2 {
                            head.len() == 19 && head[18] == 0
                        } else {
                            // Mapping family 1 is fixed by RFC 7845 (5.1 and 7.1).
                            let mapping: &[u8] = if count == 6 {
                                &[0, 4, 1, 2, 3, 5]
                            } else {
                                &[0, 6, 1, 2, 3, 4, 5, 7]
                            };
                            head.len() == 21 + count
                                && head[18] == 1
                                && head[19] == if count == 6 { 4 } else { 5 }
                                && head[20] == if count == 6 { 2 } else { 3 }
                                && &head[21..] == mapping
                        }
                    })
            })
    }
}

pub struct Stream {
    pub spec: Registration,
    pub senders: BTreeMap<(u8, u8), broadcast::Sender<Arc<Audio>>>,
    pub pending: BTreeMap<(u8, u8, u64), Assembly>,
    pub next_positions: BTreeMap<(u8, u8), u64>,
}

impl Stream {
    fn new(spec: Registration) -> Self {
        let mut senders = BTreeMap::new();
        senders.insert((1, 0), broadcast::channel(4).0);
        for variant in 0..=spec.channels() as u8 {
            senders.insert((2, variant), broadcast::channel(4).0);
        }
        Self {
            spec,
            senders,
            pending: BTreeMap::new(),
            next_positions: BTreeMap::new(),
        }
    }
}

pub struct Session {
    pub publisher: String,
    pub key: [u8; 32],
    pub streams: BTreeMap<u32, Stream>,
    pub sequences: BTreeSet<u64>,
    pub high_sequence: u64,
}

impl Session {
    pub fn new(publisher: String, key: [u8; 32]) -> Self {
        Self {
            publisher,
            key,
            streams: BTreeMap::new(),
            sequences: BTreeSet::new(),
            high_sequence: 0,
        }
    }

    pub fn register(&mut self, spec: Registration) -> Result<(), &'static str> {
        if !spec.valid() {
            return Err("invalid_registration");
        }
        if self.streams.len() >= 64 && !self.streams.contains_key(&spec.handle) {
            return Err("stream_capacity");
        }
        if self.streams.iter().any(|(handle, stream)| {
            *handle != spec.handle && stream.spec.stream_id == spec.stream_id
        }) {
            return Err("stream_occupied");
        }
        if let Some(stream) = self.streams.get_mut(&spec.handle) {
            if stream.spec.stream_id != spec.stream_id || spec.epoch < stream.spec.epoch {
                return Err("invalid_epoch");
            }
            if spec.epoch == stream.spec.epoch {
                if spec.layout != stream.spec.layout
                    || spec.pcm_rate != stream.spec.pcm_rate
                    || spec.opus_heads != stream.spec.opus_heads
                {
                    return Err("format_change_requires_epoch");
                }
                stream.spec = spec;
                return Ok(());
            }
        }
        self.streams.insert(spec.handle, Stream::new(spec));
        Ok(())
    }
}

#[derive(Default)]
pub struct Registry {
    pub sessions: HashMap<[u8; 16], Session>,
    pub offline: BTreeMap<(String, String), Instant>,
}

impl Registry {
    pub fn remove_session(&mut self, session: &[u8; 16]) {
        if let Some(session) = self.sessions.remove(session) {
            self.offline
                .retain(|_, time| time.elapsed() < Duration::from_secs(600));
            for stream in session.streams.into_values() {
                if self.offline.len() < 4096 {
                    self.offline.insert(
                        (session.publisher.clone(), stream.spec.stream_id),
                        Instant::now(),
                    );
                }
            }
        }
    }
}
