"""Test serial-to-HID path (CH9329 protocol)."""
import time


def build_frame(cmd, payload=bytes()):
    """Build a CH9329 frame with checksum."""
    head = bytes([0x57, 0xAB, 0x00, cmd, len(payload)])
    frame = head + payload
    csum = sum(frame) & 0xFF
    return frame + bytes([csum])


def hexstr(b):
    return " ".join(f"{x:02x}" for x in b)


class TestSerialHID:
    """Serial-to-HID regression tests."""

    def test_get_info(self, serial_port, flush_serial):
        """Test GET_INFO command (0x01) returns device status."""
        frame = build_frame(0x01)
        serial_port.write(frame)
        time.sleep(0.3)
        resp = serial_port.read(1024)

        assert len(resp) >= 6, f"Expected response, got {len(resp)} bytes"
        assert resp[0] == 0x57 and resp[1] == 0xAB, "Bad header"
        assert resp[3] == 0x81, f"Expected ACK for 0x01, got 0x{resp[3]:02x}"
        assert resp[4] == 8, f"Expected 8-byte payload, got {resp[4]}"

        # Status: [version_hi, version_lo, connected, led_mask, reserved...]
        status = resp[5:13]
        print(f"Device status: {hexstr(status)}")

    def test_keyboard_press_release(self, serial_port, flush_serial):
        """Test keyboard press/release (0x02) sends HID report."""
        # Key down: LShift + 'A'
        payload_down = bytes([0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00])
        frame_down = build_frame(0x02, payload_down)
        serial_port.write(frame_down)
        time.sleep(0.1)
        resp = serial_port.read(1024)

        assert len(resp) >= 6, "Expected ACK response"
        assert resp[3] == 0x82, f"Expected ACK for 0x02, got 0x{resp[3]:02x}"
        assert resp[5] == 0x00, f"Expected success (0x00), got 0x{resp[5]:02x}"

        # Key release
        payload_up = bytes([0x00] * 8)
        frame_up = build_frame(0x02, payload_up)
        serial_port.write(frame_up)
        time.sleep(0.1)
        resp = serial_port.read(1024)

        assert len(resp) >= 6, "Expected ACK for release"
        assert resp[3] == 0x82, "Expected ACK for 0x02"

    def test_relative_mouse_move(self, serial_port, flush_serial):
        """Test relative mouse move (0x05) sends HID report."""
        # dx=30, dy=20
        payload = bytes([0x00, 0x00, 30, 20, 0x00])
        frame = build_frame(0x05, payload)
        serial_port.write(frame)
        time.sleep(0.1)
        resp = serial_port.read(1024)

        assert len(resp) >= 6, "Expected ACK response"
        assert resp[3] == 0x85, f"Expected ACK for 0x05, got 0x{resp[3]:02x}"
        assert resp[5] == 0x00, f"Expected success (0x00), got 0x{resp[5]:02x}"

    def test_mouse_click(self, serial_port, flush_serial):
        """Test mouse left click (0x05) sends HID report."""
        # Button down
        payload_down = bytes([0x00, 0x01, 0x00, 0x00, 0x00])
        frame_down = build_frame(0x05, payload_down)
        serial_port.write(frame_down)
        time.sleep(0.1)

        # Button up
        payload_up = bytes([0x00, 0x00, 0x00, 0x00, 0x00])
        frame_up = build_frame(0x05, payload_up)
        serial_port.write(frame_up)
        time.sleep(0.1)
        resp = serial_port.read(1024)

        assert len(resp) >= 6, "Expected ACK response"
        assert resp[3] == 0x85, "Expected ACK for 0x05"

    def test_media_key(self, serial_port, flush_serial):
        """Test media key (0x03) sends HID report."""
        # Play/Pause key
        payload = bytes([0x02, 0x00, 0x00, 0x10])
        frame = build_frame(0x03, payload)
        serial_port.write(frame)
        time.sleep(0.1)
        resp = serial_port.read(1024)

        assert len(resp) >= 6, "Expected ACK response"
        assert resp[3] == 0x83, f"Expected ACK for 0x03, got 0x{resp[3]:02x}"
        assert resp[5] == 0x00, f"Expected success (0x00), got 0x{resp[5]:02x}"

        # Release
        payload_release = bytes([0x02, 0x00, 0x00, 0x00])
        frame_release = build_frame(0x03, payload_release)
        serial_port.write(frame_release)
        time.sleep(0.1)
