// SPDX-License-Identifier: AGPL-3.0-only
use ogg::{
    reading::{BasePacketReader, PageParser},
    Packet,
};
use std::{error::Error, io::Read};

// The Ogg library's high-level PacketReader requires Seek, which a live HTTP
// response cannot provide. Its page-fed API supports streaming without seeking.
pub struct OggReader<R> {
    reader: R,
    packets: BasePacketReader,
}

impl<R: Read> OggReader<R> {
    pub fn new(reader: R) -> Self {
        Self {
            reader,
            packets: BasePacketReader::new(),
        }
    }

    pub fn next(&mut self) -> Result<Packet, Box<dyn Error>> {
        loop {
            if let Some(packet) = self.packets.read_packet() {
                return Ok(packet);
            }
            let mut header = [0; 27];
            self.reader.read_exact(&mut header)?;
            let (mut parser, segment_count) = PageParser::new(header)?;
            let mut segments = vec![0; segment_count];
            self.reader.read_exact(&mut segments)?;
            let data_size = parser.parse_segments(segments);
            let mut data = vec![0; data_size];
            self.reader.read_exact(&mut data)?;
            self.packets.push_page(parser.parse_packet_data(data)?)?;
        }
    }
}
