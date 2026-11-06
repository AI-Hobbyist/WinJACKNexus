// SPDX-License-Identifier: AGPL-3.0-only
use cpal::traits::{DeviceTrait, HostTrait};
use std::error::Error;

pub fn list_devices() -> Result<(), Box<dyn Error>> {
    let host = cpal::default_host();
    let default = host.default_output_device();
    let mut devices = Vec::new();
    for device in host.output_devices()? {
        let formats: Vec<_> = device.supported_output_configs()?.map(|format| serde_json::json!({
            "channels": format.channels(), "minimumRate": format.min_sample_rate(),
            "maximumRate": format.max_sample_rate(), "sampleFormat": format.sample_format().to_string(),
        })).collect();
        devices.push(
            serde_json::json!({"id": device.id()?.to_string(), "name": device.to_string(),
            "default": default.as_ref() == Some(&device), "formats": formats}),
        );
    }
    println!("{}", serde_json::Value::Array(devices));
    Ok(())
}

pub fn play<R: std::io::Read>(
    decoder: &mut crate::decode::Decoder<R>,
    frame_limit: u64,
    device_id: Option<&str>,
) -> Result<(), Box<dyn Error>> {
    use cpal::traits::StreamTrait;
    use std::sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        Arc,
    };
    use std::time::{Duration, Instant};

    let format = decoder.format;
    let host = cpal::default_host();
    let device = if let Some(id) = device_id {
        let mut selected = None;
        for device in host.output_devices()? {
            if device.id()?.to_string() == id {
                selected = Some(device);
                break;
            }
        }
        selected.ok_or("output device not found")?
    } else {
        host.default_output_device()
            .ok_or("no default output device")?
    };
    let supported = device
        .supported_output_configs()?
        .find(|config| {
            config.channels() as usize == format.channels
                && config.min_sample_rate() <= format.rate
                && config.max_sample_rate() >= format.rate
                && config.sample_format() == cpal::SampleFormat::F32
        })
        .ok_or("output device does not support the original layout/rate as float32")?;
    let config = supported.with_sample_rate(format.rate).config();
    // Queue complete frames: an underrun must never rotate speaker positions.
    let channels = format.channels;
    let capacity = format.rate as usize / 10;
    let (mut producer, mut consumer) = rtrb::RingBuffer::<[f32; 8]>::new(capacity);
    let callbacks = Arc::new(AtomicU64::new(0));
    let played_samples = Arc::new(AtomicU64::new(0));
    let failed = Arc::new(AtomicBool::new(false));
    let callback_count = callbacks.clone();
    let sample_count = played_samples.clone();
    let stream_failed = failed.clone();
    let stream = device.build_output_stream(
        config,
        move |output: &mut [f32], _: &cpal::OutputCallbackInfo| {
            callback_count.fetch_add(1, Ordering::Relaxed);
            let mut consumed = 0;
            for frame in output.chunks_exact_mut(channels) {
                match consumer.pop() {
                    Ok(value) => {
                        consumed += channels as u64;
                        frame.copy_from_slice(&value[..channels]);
                    }
                    Err(_) => frame.fill(0.0),
                }
            }
            sample_count.fetch_add(consumed, Ordering::Relaxed);
        },
        move |_| {
            stream_failed.store(true, Ordering::Relaxed);
        },
        None,
    )?;
    let mut queued_frames = 0u64;
    let mut started = false;
    let block_frames = (format.rate as usize / 50).min(960);
    while queued_frames < frame_limit {
        let data =
            decoder.next_chunk((frame_limit - queued_frames).min(block_frames as u64) as usize)?;
        let frames = (data.len() / format.channels).min((frame_limit - queued_frames) as usize);
        for frame in data[..frames * format.channels].chunks_exact(channels) {
            let mut pending = [0.0; 8];
            pending[..channels].copy_from_slice(frame);
            let deadline = Instant::now() + Duration::from_secs(2);
            loop {
                match producer.push(pending) {
                    Ok(()) => break,
                    Err(rtrb::PushError::Full(value)) => pending = value,
                }
                if failed.load(Ordering::Relaxed) || Instant::now() >= deadline {
                    return Err("output device stopped consuming audio".into());
                }
                std::thread::sleep(Duration::from_millis(1));
            }
        }
        queued_frames += frames as u64;
        if !started
            && (queued_frames >= u64::from(format.rate) * 40 / 1000 || queued_frames == frame_limit)
        {
            stream.play()?;
            started = true;
        }
    }
    let deadline = Instant::now() + Duration::from_secs(2);
    while played_samples.load(Ordering::Relaxed) < queued_frames * format.channels as u64 {
        if failed.load(Ordering::Relaxed) || Instant::now() >= deadline {
            return Err("output device did not drain queued audio".into());
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    stream.pause()?;
    println!(
        "{}",
        serde_json::json!({
            "channels": format.channels, "sampleRate": format.rate,
            "playedFrames": played_samples.load(Ordering::Relaxed) / format.channels as u64,
            "outputCallbacks": callbacks.load(Ordering::Relaxed),
        })
    );
    Ok(())
}
