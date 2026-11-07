// SPDX-License-Identifier: AGPL-3.0-only
mod decode;
mod ogg_reader;
mod output;
use std::{error::Error, io::Write, time::Duration};

fn main() -> Result<(), Box<dyn Error>> {
    let mut arguments = std::env::args().skip(1);
    let mut url = None;
    let mut check = false;
    let mut frame_limit = u64::MAX;
    let mut list_devices = false;
    let mut device = None;
    let mut reconnect = false;
    let mut pcm_out = None;
    while let Some(argument) = arguments.next() {
        match argument.as_str() {
            "--url" => url = Some(arguments.next().ok_or("--url needs a value")?),
            "--check" => check = true,
            "--list-devices" => list_devices = true,
            "--device" => device = Some(arguments.next().ok_or("--device needs an ID")?),
            "--reconnect" => reconnect = true,
            "--pcm-out" => pcm_out = Some(arguments.next().ok_or("--pcm-out needs a file")?),
            "--frames" => {
                frame_limit = arguments.next().ok_or("--frames needs a value")?.parse()?
            }
            _ => return Err(format!("unknown option: {argument}").into()),
        }
    }
    if list_devices {
        return output::list_devices();
    }
    let url = url.ok_or("usage: bc-listen --url HTTP_AUDIO_URL [--check --frames N]")?;
    if frame_limit == 0 {
        return Err("frame count must be positive".into());
    }
    if pcm_out.is_some() && !check {
        return Err("--pcm-out requires --check".into());
    }
    let client = reqwest::blocking::Client::builder()
        .connect_timeout(Duration::from_secs(5))
        .timeout(None)
        .build()?;
    let mut delay = 1;
    loop {
        match subscribe(
            &client,
            &url,
            check,
            frame_limit,
            device.as_deref(),
            pcm_out.as_deref(),
        ) {
            Ok(()) => return Ok(()),
            Err(error) if reconnect && retryable(error.as_ref()) => {
                eprintln!("subscription interrupted: {error}; reconnecting in {delay}s");
                std::thread::sleep(Duration::from_secs(delay));
                delay = (delay * 2).min(30);
            }
            Err(error) => return Err(error),
        }
    }
}

fn retryable(error: &(dyn Error + 'static)) -> bool {
    if let Some(error) = error.downcast_ref::<std::io::Error>() {
        return matches!(
            error.kind(),
            std::io::ErrorKind::UnexpectedEof
                | std::io::ErrorKind::ConnectionReset
                | std::io::ErrorKind::ConnectionAborted
                | std::io::ErrorKind::TimedOut
                | std::io::ErrorKind::Other
        );
    }
    if let Some(error) = error.downcast_ref::<reqwest::Error>() {
        return error.is_connect()
            || error.is_timeout()
            || error.status() == Some(reqwest::StatusCode::SERVICE_UNAVAILABLE);
    }
    false
}

fn subscribe(
    client: &reqwest::blocking::Client,
    url: &str,
    check: bool,
    frame_limit: u64,
    device: Option<&str>,
    pcm_out: Option<&str>,
) -> Result<(), Box<dyn Error>> {
    let response = client.get(url).send()?.error_for_status()?;
    let content_type = response
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|value| value.to_str().ok())
        .unwrap_or("")
        .to_owned();
    let mut decoder = decode::Decoder::new(response, &content_type)?;
    if !check {
        return output::play(&mut decoder, frame_limit, device);
    }
    let format = decoder.format;
    let mut peaks = vec![0.0f32; format.channels];
    let mut pcm_file = pcm_out
        .map(std::fs::File::create)
        .transpose()?
        .map(std::io::BufWriter::new);
    let mut frames = 0u64;
    while frames < frame_limit {
        let data = decoder.next_chunk((frame_limit - frames).min(1024) as usize)?;
        let count = (data.len() / format.channels).min((frame_limit - frames) as usize);
        for (index, sample) in data[..count * format.channels].iter().enumerate() {
            let channel = index % format.channels;
            peaks[channel] = peaks[channel].max(sample.abs());
            if let Some(file) = &mut pcm_file {
                file.write_all(&sample.to_le_bytes())?;
            }
        }
        frames += count as u64;
    }
    if let Some(file) = &mut pcm_file {
        file.flush()?;
    }
    println!(
        "{}",
        serde_json::json!({"channels": format.channels, "sampleRate": format.rate, "frames": frames, "peaks": peaks, "concealedFrames": decoder.concealed_frames})
    );
    Ok(())
}
