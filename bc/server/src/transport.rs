// SPDX-License-Identifier: AGPL-3.0-only
use crate::{registry::Registry, App};
use hmac::{Hmac, Mac};
use sha2::Sha256;
use std::{
    collections::BTreeSet,
    sync::Arc,
    time::{Duration, Instant},
};
use tokio::net::UdpSocket;

pub struct Audio {
    pub position: u64,
    pub samples: u16,
    pub data: Vec<u8>,
    pub created: Instant,
}

pub struct Assembly {
    first: Instant,
    samples: u16,
    fragments: Vec<Option<Vec<u8>>>,
}

pub async fn receive(socket: Arc<UdpSocket>, app: Arc<App>) {
    let mut packet = [0; 65536];
    let mut flush_timer = tokio::time::interval(Duration::from_millis(5));
    loop {
        tokio::select! {
            result = socket.recv_from(&mut packet) => {
                match result {
                    Ok((size, _)) => consume(&packet[..size], &mut app.registry.lock().unwrap()),
                    Err(error) => { eprintln!("UDP receive failed: {error}"); break; }
                }
            }
            _ = flush_timer.tick() => flush(&mut app.registry.lock().unwrap()),
        }
    }
}

fn consume(packet: &[u8], registry: &mut Registry) {
    if packet.len() < 97
        || packet.len() > 1200
        || &packet[..4] != b"WJBC"
        || packet[4] != 1
        || !matches!(packet[5], 1 | 2)
        || packet[7] != 0
        || packet[54..56] != [0, 0]
        || packet[60..64] != [0, 0, 0, 0]
    {
        return;
    }
    let session_id: [u8; 16] = packet[8..24].try_into().unwrap();
    let Some(session) = registry.sessions.get_mut(&session_id) else {
        return;
    };
    let mut mac = Hmac::<Sha256>::new_from_slice(&session.key).unwrap();
    mac.update(&packet[..packet.len() - 32]);
    if mac.verify_slice(&packet[packet.len() - 32..]).is_err() {
        return;
    }
    let u16_at = |offset| u16::from_le_bytes(packet[offset..offset + 2].try_into().unwrap());
    let u32_at = |offset| u32::from_le_bytes(packet[offset..offset + 4].try_into().unwrap());
    let u64_at = |offset| u64::from_le_bytes(packet[offset..offset + 8].try_into().unwrap());
    let (codec, variant) = (packet[5], packet[6]);
    let sequence = u64_at(32);
    if sequence.saturating_add(16384) <= session.high_sequence
        || session.sequences.contains(&sequence)
    {
        return;
    }
    let Some(stream) = session.streams.get_mut(&u32_at(24)) else {
        return;
    };
    let (samples, index, count) = (u16_at(48), u16_at(50) as usize, u16_at(52) as usize);
    let size = u32_at(56) as usize;
    let position = u64_at(40);
    if u32_at(28) != stream.spec.epoch
        || !stream.senders.contains_key(&(codec, variant))
        || samples == 0
        || samples as u32
            > if codec == 1 {
                stream.spec.pcm_rate / 50
            } else {
                960
            }
        || count == 0
        || count > 32
        || index >= count
        || size == 0
        || size > 1104
        || packet.len() != size + 96
        || (index + 1 < count && size != 1104)
        || position.checked_add(samples as u64).is_none()
    {
        return;
    }
    if stream
        .next_positions
        .get(&(codec, variant))
        .is_some_and(|next| position < *next)
    {
        return;
    }
    let assembly_key = (codec, variant, position);
    let now = Instant::now();
    stream
        .pending
        .retain(|_, assembly| now.duration_since(assembly.first) < Duration::from_millis(100));
    if !stream.pending.contains_key(&assembly_key)
        && stream
            .pending
            .keys()
            .filter(|key| key.0 == codec && key.1 == variant)
            .count()
            >= 8
    {
        return;
    }
    let assembly = stream
        .pending
        .entry(assembly_key)
        .or_insert_with(|| Assembly {
            first: now,
            samples,
            fragments: vec![None; count],
        });
    if assembly.samples != samples
        || assembly.fragments.len() != count
        || assembly.fragments[index].is_some()
    {
        return;
    }
    assembly.fragments[index] = Some(packet[64..64 + size].to_vec());
    session.sequences.insert(sequence);
    session.high_sequence = session.high_sequence.max(sequence);
    let cutoff = session.high_sequence.saturating_sub(16383);
    session.sequences = session.sequences.split_off(&cutoff);
}

fn flush(registry: &mut Registry) {
    for session in registry.sessions.values_mut() {
        for stream in session.streams.values_mut() {
            let keys: Vec<_> = stream.pending.keys().copied().collect();
            let mut waiting = BTreeSet::new();
            for (codec, variant, position) in keys {
                if waiting.contains(&(codec, variant)) {
                    continue;
                }
                let assembly = &stream.pending[&(codec, variant, position)];
                if assembly.first.elapsed() < Duration::from_millis(40) {
                    // A later-position block must not overtake an earlier one
                    // merely because it arrived first over UDP.
                    waiting.insert((codec, variant));
                    continue;
                }
                let complete = assembly.fragments.iter().all(Option::is_some);
                let assembly = stream.pending.remove(&(codec, variant, position)).unwrap();
                if !complete {
                    continue;
                }
                let data: Vec<u8> = assembly.fragments.into_iter().flatten().flatten().collect();
                if codec == 1
                    && data.len() != assembly.samples as usize * stream.spec.channels() * 2
                {
                    continue;
                }
                if stream
                    .next_positions
                    .get(&(codec, variant))
                    .is_some_and(|next| position < *next)
                {
                    continue;
                }
                stream
                    .next_positions
                    .insert((codec, variant), position + assembly.samples as u64);
                let audio = Arc::new(Audio {
                    position,
                    samples: assembly.samples,
                    data,
                    created: Instant::now(),
                });
                let _ = stream.senders[&(codec, variant)].send(audio);
            }
        }
    }
}
