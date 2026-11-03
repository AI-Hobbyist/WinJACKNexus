// SPDX-License-Identifier: AGPL-3.0-only
// Ogg framing only: encoded Opus packets are never decoded or modified here.
pub struct Writer {
    serial: u32,
    sequence: u32,
}

impl Writer {
    pub fn new() -> Self {
        Self {
            serial: rand::random(),
            sequence: 0,
        }
    }

    pub fn page(&mut self, packet: &[u8], granule: u64, beginning: bool) -> Vec<u8> {
        let segments = packet.len() / 255 + 1;
        let mut page = Vec::with_capacity(27 + segments + packet.len());
        page.extend_from_slice(b"OggS");
        page.extend_from_slice(&[0, if beginning { 2 } else { 0 }]);
        page.extend_from_slice(&granule.to_le_bytes());
        page.extend_from_slice(&self.serial.to_le_bytes());
        page.extend_from_slice(&self.sequence.to_le_bytes());
        page.extend_from_slice(&[0; 4]);
        page.push(segments as u8);
        page.extend(std::iter::repeat_n(255, segments - 1));
        page.push((packet.len() % 255) as u8);
        page.extend_from_slice(packet);
        let mut crc = 0u32;
        for byte in &page {
            crc ^= (*byte as u32) << 24;
            for _ in 0..8 {
                crc = if crc & 0x80000000 != 0 {
                    (crc << 1) ^ 0x04c11db7
                } else {
                    crc << 1
                };
            }
        }
        page[22..26].copy_from_slice(&crc.to_le_bytes());
        self.sequence = self.sequence.wrapping_add(1);
        page
    }
}

pub fn tags() -> Vec<u8> {
    let vendor = b"WinJACKNexus broadcast relay";
    let mut packet = b"OpusTags".to_vec();
    packet.extend_from_slice(&(vendor.len() as u32).to_le_bytes());
    packet.extend_from_slice(vendor);
    packet.extend_from_slice(&0u32.to_le_bytes());
    packet
}
