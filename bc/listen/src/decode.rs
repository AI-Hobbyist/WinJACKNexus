// SPDX-License-Identifier: AGPL-3.0-only
use crate::ogg_reader::OggReader;
use std::{error::Error, io::Read};

#[derive(Clone, Copy)]
pub struct Format {
    pub channels: usize,
    pub rate: u32,
}

enum Codec<R> {
    Wave(R),
    Opus {
        reader: OggReader<R>,
        decoder: opus::MSDecoder,
        skip: usize,
        gain: f32,
        position: u64,
    },
}

pub struct Decoder<R> {
    pub format: Format,
    pub concealed_frames: u64,
    codec: Codec<R>,
}

impl<R: Read> Decoder<R> {
    pub fn new(mut reader: R, content_type: &str) -> Result<Self, Box<dyn Error>> {
        let media_type = content_type.split(';').next().unwrap_or("").trim();
        if media_type.eq_ignore_ascii_case("audio/wav") {
            let format = wave_header(&mut reader)?;
            return Ok(Self {
                format,
                concealed_frames: 0,
                codec: Codec::Wave(reader),
            });
        }
        if !media_type.eq_ignore_ascii_case("audio/ogg") {
            return Err("unsupported audio Content-Type".into());
        }
        let mut reader = OggReader::new(reader);
        let head = reader.next()?.data;
        if head.len() < 19 || &head[..8] != b"OpusHead" || head[8] != 1 {
            return Err("invalid OpusHead".into());
        }
        let channels = head[9] as usize;
        if !matches!(channels, 1 | 2 | 6 | 8) {
            return Err("unsupported Opus channel layout".into());
        }
        let skip = u16::from_le_bytes(head[10..12].try_into().unwrap()) as usize;
        let gain_db = i16::from_le_bytes(head[16..18].try_into().unwrap()) as f32 / 256.0;
        let (streams, coupled, mapping) = match (channels, head[18]) {
            (1, 0) if head.len() == 19 => (1, 0, vec![0]),
            (2, 0) if head.len() == 19 => (1, 1, vec![0, 1]),
            (6 | 8, 1) if head.len() == 21 + channels => (head[19], head[20], head[21..].to_vec()),
            _ => return Err("unsupported Opus mapping".into()),
        };
        let decoder = opus::MSDecoder::new(48000, streams, coupled, &mapping)?;
        if !reader.next()?.data.starts_with(b"OpusTags") {
            return Err("missing OpusTags".into());
        }
        Ok(Self {
            format: Format {
                channels,
                rate: 48000,
            },
            concealed_frames: 0,
            codec: Codec::Opus {
                reader,
                decoder,
                skip,
                gain: 10.0f32.powf(gain_db / 20.0),
                position: 0,
            },
        })
    }

    pub fn next_chunk(&mut self, maximum_frames: usize) -> Result<Vec<f32>, Box<dyn Error>> {
        let channels = self.format.channels;
        match &mut self.codec {
            Codec::Wave(reader) => {
                let mut bytes = vec![0; maximum_frames * channels * 2];
                reader.read_exact(&mut bytes)?;
                Ok(bytes
                    .chunks_exact(2)
                    .map(|sample| i16::from_le_bytes(sample.try_into().unwrap()) as f32 / 32768.0)
                    .collect())
            }
            Codec::Opus {
                reader,
                decoder,
                skip,
                gain,
                position,
            } => {
                let packet = reader.next()?;
                let packet_frames = opus::packet::get_nb_samples(&packet.data, 48000)?;
                let expected_end = *position + packet_frames as u64;
                let granule = packet.absgp_page();
                let gap = if packet.last_in_page() && granule != u64::MAX {
                    granule.saturating_sub(expected_end) as usize
                } else {
                    0
                };
                if gap > 4800 || gap % 120 != 0 {
                    return Err("Opus timeline discontinuity exceeds live buffer".into());
                }
                let mut decoded = vec![0.0; (gap + 5760) * channels];
                if gap > 0 {
                    decoder.decode_float(&[], &mut decoded[..gap * channels], false)?;
                    self.concealed_frames += gap as u64;
                }
                let frames = gap
                    + decoder.decode_float(&packet.data, &mut decoded[gap * channels..], false)?;
                *position = expected_end + gap as u64;
                let trim = (*skip).min(frames);
                *skip -= trim;
                // libopus returns Vorbis family-1 order. The device/WAV side
                // uses ascending Windows speaker-mask order, including backs
                // before sides for 7.1.
                let order: &[usize] = match channels {
                    6 => &[0, 2, 1, 5, 3, 4],
                    8 => &[0, 2, 1, 7, 5, 6, 3, 4],
                    2 => &[0, 1],
                    _ => &[0],
                };
                let mut output = Vec::with_capacity((frames - trim) * channels);
                for frame in decoded[trim * channels..frames * channels].chunks_exact(channels) {
                    for channel in order {
                        output.push(frame[*channel] * *gain);
                    }
                }
                Ok(output)
            }
        }
    }
}

fn wave_header(reader: &mut impl Read) -> Result<Format, Box<dyn Error>> {
    let mut header = [0; 12];
    reader.read_exact(&mut header)?;
    if &header[..4] != b"RIFF" || &header[8..] != b"WAVE" {
        return Err("response is not standard RIFF/WAVE".into());
    }
    let mut format = None;
    loop {
        let mut chunk = [0; 8];
        reader.read_exact(&mut chunk)?;
        let size = u32::from_le_bytes(chunk[4..].try_into().unwrap()) as usize;
        match &chunk[..4] {
            b"fmt " => {
                if !(16..=4096).contains(&size) {
                    return Err("invalid WAV format chunk".into());
                }
                let mut data = vec![0; size + size % 2];
                reader.read_exact(&mut data)?;
                let tag = u16::from_le_bytes(data[..2].try_into().unwrap());
                let channels = u16::from_le_bytes(data[2..4].try_into().unwrap()) as usize;
                let rate = u32::from_le_bytes(data[4..8].try_into().unwrap());
                let alignment = u16::from_le_bytes(data[12..14].try_into().unwrap()) as usize;
                let bits = u16::from_le_bytes(data[14..16].try_into().unwrap());
                let pcm_guid = [1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113];
                let pcm = tag == 1 || (tag == 0xfffe && size >= 40 && data[24..40] == pcm_guid);
                if !pcm
                    || bits != 16
                    || !matches!(channels, 1 | 2 | 6 | 8)
                    || rate == 0
                    || alignment != channels * 2
                {
                    return Err("unsupported WAV layout or sample format".into());
                }
                format = Some(Format { channels, rate });
            }
            b"data" => return format.ok_or_else(|| "WAV data precedes format".into()),
            _ => {
                if size > 1024 * 1024 {
                    return Err("WAV metadata chunk is too large".into());
                }
                let mut skip = vec![0; size + size % 2];
                reader.read_exact(&mut skip)?;
            }
        }
    }
}
