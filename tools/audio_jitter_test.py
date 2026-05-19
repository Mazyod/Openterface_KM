"""
Audio Jitter Test for Openterface USB Audio Device
---------------------------------------------------
1. Lists all audio devices and lets you pick the USB device for playback & recording.
2. Plays a packet-slip-sensitive sine tone through the selected output device.
3. Simultaneously records from the selected input device.
4. Analyses the recording for:
   - THD (Total Harmonic Distortion)
   - Dropout / silence gaps
   - Frequency drift / jitter (instantaneous frequency via zero-crossing)
   - SNR
5. Saves plots to audio_jitter_report.png
"""

import argparse
import numpy as np
import sounddevice as sd
import scipy.signal as signal
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import sys

# ── Configuration ────────────────────────────────────────────────────────────
SAMPLE_RATE   = 16000   # Hz  (firmware UAC descriptor: 16 kHz)
DURATION      = 5.0     # seconds to record
TONE_FREQ     = 997     # Hz  test tone; avoids 1 ms USB packet-cycle alignment
TONE_AMPLITUDE= 0.8     # 0.0 – 1.0
CHANNELS      = 2       # firmware UAC descriptor: stereo
BLOCKSIZE     = 0       # let PortAudio/host choose a stable block size
OUTPUT_PNG    = "audio_jitter_report.png"
USB_PACKET_MS = 1.0     # full-speed UAC isochronous audio packet interval
# ─────────────────────────────────────────────────────────────────────────────


def list_devices():
    devices = sd.query_devices()
    print("\n=== Audio Devices ===")
    for i, d in enumerate(devices):
        tag = ""
        if d['max_input_channels'] > 0 and d['max_output_channels'] > 0:
            tag = "[IN/OUT]"
        elif d['max_input_channels'] > 0:
            tag = "[IN]    "
        elif d['max_output_channels'] > 0:
            tag = "[OUT]   "
        print(
            f"  {i:2d}: {tag} {d['name']}"
            f"  in={d['max_input_channels']} out={d['max_output_channels']}"
            f" default_sr={d['default_samplerate']:.0f}"
        )
    print()
    return devices


def pick_device(devices, direction: str):
    """direction: 'input' or 'output'"""
    key = f'max_{direction}_channels'
    candidates = [(i, d) for i, d in enumerate(devices) if d[key] > 0]
    usb_candidates = [(i, d) for i, d in candidates
                      if 'usb' in d['name'].lower() or 'openterface' in d['name'].lower()]

    print(f"--- Select {direction.upper()} device ---")
    if usb_candidates:
        print("  (USB devices highlighted with *)")
    for i, d in candidates:
        marker = "*" if (i, d) in usb_candidates else " "
        print(f"  {marker}{i:2d}: {d['name']}")

    default = usb_candidates[0][0] if usb_candidates else candidates[0][0]
    choice = input(f"Enter device index [{default}]: ").strip()
    return int(choice) if choice else default


def generate_tone(duration, freq, fs, amplitude, channels):
    t = np.linspace(0, duration, int(fs * duration), endpoint=False)
    mono = (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)
    return np.repeat(mono[:, None], channels, axis=1)


def generate_silence(duration, fs, channels):
    return np.zeros((int(fs * duration), channels), dtype=np.float32)


def play_and_record(tone, out_dev, in_dev, fs, channels, tone_freq):
    if tone_freq is None:
        print(f"\nPlaying digital silence at {fs} Hz, {channels} channel(s), and recording ...")
    else:
        print(f"\nPlaying {tone_freq:g} Hz tone at {fs} Hz, {channels} channel(s), and recording ...")
    recorded = sd.playrec(
        tone,
        samplerate=fs,
        channels=channels,
        device=(in_dev, out_dev),
        dtype='float32',
        blocksize=BLOCKSIZE,
    )
    sd.wait()
    return recorded


def record_only(in_dev, fs, channels, duration):
    print(f"\nRecording only at {fs} Hz, {channels} channel(s), {duration:.1f}s ...")
    recorded = sd.rec(
        int(duration * fs),
        samplerate=fs,
        channels=channels,
        device=in_dev,
        dtype='float32',
        blocksize=BLOCKSIZE,
    )
    sd.wait()
    return recorded


def mix_down(audio):
    if audio.ndim == 1:
        return audio
    return np.mean(audio, axis=1)


# ── Analysis helpers ──────────────────────────────────────────────────────────

def compute_snr(rec, fs, tone_freq):
    freqs, psd = signal.welch(rec, fs, nperseg=4096)
    idx = np.argmin(np.abs(freqs - tone_freq))
    signal_power = psd[idx]
    noise_mask = np.ones(len(psd), dtype=bool)
    noise_mask[max(0, idx-5):idx+6] = False
    noise_power = np.mean(psd[noise_mask])
    snr_db = 10 * np.log10(signal_power / noise_power) if noise_power > 0 else float('inf')
    return snr_db, freqs, psd


def compute_thd(rec, fs, tone_freq, n_harmonics=5):
    N = len(rec)
    spectrum = np.abs(np.fft.rfft(rec)) / N
    freqs_fft = np.fft.rfftfreq(N, 1.0/fs)

    def nearest_bin(f):
        return int(np.argmin(np.abs(freqs_fft - f)))

    fundamental_power = spectrum[nearest_bin(tone_freq)] ** 2
    harmonic_power = sum(
        spectrum[nearest_bin(tone_freq * h)] ** 2
        for h in range(2, n_harmonics + 1)
    )
    thd = np.sqrt(harmonic_power / fundamental_power) * 100 if fundamental_power > 0 else 0
    return thd, spectrum, freqs_fft


def detect_dropouts(rec, fs, threshold=0.01, min_gap_ms=5):
    """Find silence gaps longer than min_gap_ms that likely indicate dropouts."""
    silent = np.abs(rec) < threshold
    gap_samples = int(min_gap_ms * fs / 1000)
    dropouts = []
    in_gap = False
    gap_start = 0
    for i, s in enumerate(silent):
        if s and not in_gap:
            in_gap = True
            gap_start = i
        elif not s and in_gap:
            in_gap = False
            length = i - gap_start
            if length >= gap_samples:
                dropouts.append((gap_start / fs, length / fs * 1000))
    return dropouts


def instantaneous_frequency(rec, fs):
    """Estimate instantaneous frequency via Hilbert transform."""
    analytic = signal.hilbert(rec)
    inst_phase = np.unwrap(np.angle(analytic))
    inst_freq = np.diff(inst_phase) / (2.0 * np.pi) * fs
    return inst_freq


def rms_dbfs(rec):
    rms = np.sqrt(np.mean(np.square(rec))) if len(rec) else 0.0
    return 20 * np.log10(max(float(rms), 1e-12))


def compute_tone_fit_noise(rec, fs, tone_freq, skip_start=0.25):
    """Fit the expected sine and report residual noise/distortion.

    The Welch-bin SNR is useful for finding a dominant tone, but it can miss
    audible broadband hiss, modulation, or packet-slip residue around the tone.
    A least-squares sine fit exposes those artifacts as residual RMS/SINAD.
    """
    skip = min(int(skip_start * fs), max(0, len(rec) - 1))
    rec = rec[skip:]
    if len(rec) == 0:
        return 0.0, 0.0, 0.0, 0.0

    t = np.arange(len(rec)) / fs
    basis = np.column_stack([
        np.sin(2 * np.pi * tone_freq * t),
        np.cos(2 * np.pi * tone_freq * t),
        np.ones_like(t),
    ])
    coeffs, *_ = np.linalg.lstsq(basis, rec, rcond=None)
    fitted = basis @ coeffs
    fitted_ac = fitted - np.mean(fitted)
    residual = rec - fitted

    signal_rms = np.sqrt(np.mean(np.square(fitted_ac)))
    residual_rms = np.sqrt(np.mean(np.square(residual)))
    sinad_db = 20 * np.log10(max(float(signal_rms), 1e-12) / max(float(residual_rms), 1e-12))
    corr = np.corrcoef(rec, fitted)[0, 1] if len(rec) > 1 else 0.0
    return rms_dbfs(fitted_ac), rms_dbfs(residual), sinad_db, corr


def is_packet_aligned_tone(tone_freq, packet_ms=USB_PACKET_MS, tolerance=1e-6):
    cycles_per_packet = tone_freq * packet_ms / 1000.0
    return abs(cycles_per_packet - round(cycles_per_packet)) < tolerance


def print_packet_alignment_warning(tone_freq):
    if is_packet_aligned_tone(tone_freq):
        print(
            "\nWARNING: The selected tone is aligned to the 1 ms USB audio packet interval.\n"
            "Whole-packet repeats or drops can be much less audible/measurable with this tone.\n"
            "Use a non-aligned tone such as 997 Hz for jitter and packet-slip checks."
        )


# ── Plotting ──────────────────────────────────────────────────────────────────

def make_report(rec, fs, tone_freq, snr_db, thd, dropouts, inst_freq, spectrum, freqs_fft, psd, freqs_psd, output_png):
    t = np.linspace(0, len(rec)/fs, len(rec))
    fig, axes = plt.subplots(4, 1, figsize=(12, 14))
    fig.suptitle(f"Audio Jitter Analysis  |  Tone: {tone_freq} Hz  |  SR: {fs} Hz", fontsize=13)

    # 1. Waveform
    ax = axes[0]
    ax.plot(t, rec, linewidth=0.4, color='steelblue')
    for drop_t, _ in dropouts:
        ax.axvline(drop_t, color='red', linestyle='--', alpha=0.7, linewidth=0.8)
    ax.set_title(f"Waveform  (dropouts={len(dropouts)})")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude")
    ax.set_xlim(0, t[-1])

    # 2. Power Spectral Density
    ax = axes[1]
    ax.semilogy(freqs_psd, psd, color='darkorange', linewidth=0.8)
    ax.axvline(tone_freq, color='green', linestyle='--', label=f'{tone_freq} Hz fundamental')
    ax.set_title(f"PSD  |  SNR: {snr_db:.1f} dB")
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Power")
    ax.set_xlim(0, fs/2)
    ax.legend()

    # 3. FFT spectrum (log, first 5 kHz)
    ax = axes[2]
    mask = freqs_fft <= 5000
    ax.plot(freqs_fft[mask], 20*np.log10(spectrum[mask]+1e-12), color='purple', linewidth=0.6)
    for h in range(1, 6):
        ax.axvline(tone_freq * h, color='red', linestyle=':', alpha=0.5, linewidth=0.7,
                   label=f'{tone_freq*h} Hz' if h == 1 else '')
    ax.set_title(f"FFT Spectrum (0–5 kHz)  |  THD: {thd:.2f}%")
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("dB")
    ax.legend()

    # 4. Instantaneous frequency (jitter view)
    ax = axes[3]
    t_if = np.linspace(0, len(rec)/fs, len(inst_freq))
    # only show samples near fundamental to reduce noise
    mask_if = (inst_freq > tone_freq * 0.5) & (inst_freq < tone_freq * 1.5)
    inst_freq_clean = np.where(mask_if, inst_freq, np.nan)
    ax.plot(t_if, inst_freq_clean, linewidth=0.3, color='teal')
    ax.axhline(tone_freq, color='red', linestyle='--', linewidth=0.8, label='Ideal')
    jitter_std = np.nanstd(inst_freq_clean)
    ax.set_title(f"Instantaneous Frequency (Jitter σ = {jitter_std:.2f} Hz)")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Freq (Hz)")
    ax.set_ylim(tone_freq - 200, tone_freq + 200)
    ax.legend()

    plt.tight_layout()
    plt.savefig(output_png, dpi=150)
    print(f"\nReport saved -> {output_png}")


# ── Main ──────────────────────────────────────────────────────────────────────

def parse_args():
    parser = argparse.ArgumentParser(description="Loopback audio bridge diagnostic for Openterface UAC firmware")
    parser.add_argument('--list', action='store_true', help='list audio devices and exit')
    parser.add_argument('--output', type=int, help='output device index to play the test tone')
    parser.add_argument('--input', type=int, help='input device index to record the bridged tone')
    parser.add_argument('--record-only', action='store_true', help='record input only, useful for firmware-generated tone diagnostics')
    parser.add_argument('--rate', type=int, default=SAMPLE_RATE, help='sample rate, default: firmware-native 16000')
    parser.add_argument('--channels', type=int, default=CHANNELS, help='channel count, default: firmware-native stereo')
    parser.add_argument('--duration', type=float, default=DURATION, help='test duration in seconds')
    parser.add_argument('--tone', type=float, default=TONE_FREQ, help='test tone frequency in Hz, default: 997 to avoid 1 ms packet alignment')
    parser.add_argument('--amplitude', type=float, default=TONE_AMPLITUDE, help='tone amplitude 0.0..1.0')
    parser.add_argument('--silence-output', action='store_true', help='play digital silence and report recorded noise floor')
    parser.add_argument('--skip-start', type=float, default=0.25, help='seconds to skip for tone-fit noise metrics')
    parser.add_argument('--report', default=OUTPUT_PNG, help='output PNG report path')
    return parser.parse_args()


def main():
    args = parse_args()
    devices = list_devices()
    if args.list:
        return

    out_dev = args.output if args.output is not None else (None if args.record_only else pick_device(devices, 'output'))
    in_dev = args.input if args.input is not None else pick_device(devices, 'input')

    if out_dev is not None:
        print(f"\nOutput : [{out_dev}] {devices[out_dev]['name']}")
    print(f"Input  : [{in_dev}]  {devices[in_dev]['name']}")
    print(f"Format : {args.rate} Hz, {args.channels} channel(s), {args.duration:.1f}s")
    print_packet_alignment_warning(args.tone)

    if args.record_only:
        rec_multi = record_only(in_dev, args.rate, args.channels, args.duration)
    else:
        if args.silence_output:
            tone = generate_silence(args.duration, args.rate, args.channels)
            play_freq = None
        else:
            tone = generate_tone(args.duration, args.tone, args.rate, args.amplitude, args.channels)
            play_freq = args.tone
        rec_multi = play_and_record(tone, out_dev, in_dev, args.rate, args.channels, play_freq)
    rec = mix_down(rec_multi)

    if np.max(np.abs(rec)) < 0.001 and not args.silence_output:
        print("\nWARNING: Recording appears silent. Check device selection, Windows sound levels, and USB connections.")
        sys.exit(1)

    print(f"Recorded {len(rec)} samples, peak amplitude: {np.max(np.abs(rec)):.4f}")

    if args.silence_output:
        skip = min(int(args.skip_start * args.rate), max(0, len(rec) - 1))
        noise = rec[skip:] - np.mean(rec[skip:])
        print("\n=== Noise Floor Results ===")
        print(f"  Peak        : {np.max(np.abs(noise)):.6f}")
        print(f"  RMS noise   : {rms_dbfs(noise):.1f} dBFS")
        return

    snr_db, freqs_psd, psd = compute_snr(rec, args.rate, args.tone)
    thd, spectrum, freqs_fft = compute_thd(rec, args.rate, args.tone)
    dropouts = detect_dropouts(rec, args.rate)
    inst_freq = instantaneous_frequency(rec, args.rate)

    print("\n=== Analysis Results ===")
    print(f"  SNR         : {snr_db:.1f} dB  (>60 dB is good)")
    print(f"  THD         : {thd:.2f}%    (<1% is good)")
    print(f"  Dropouts    : {len(dropouts)}")
    if dropouts:
        for ts, dur_ms in dropouts:
            print(f"               t={ts:.3f}s  duration={dur_ms:.1f}ms")
    jitter_std = np.nanstd(np.where(
        (inst_freq > args.tone * 0.5) & (inst_freq < args.tone * 1.5), inst_freq, np.nan))
    print(f"  Freq jitter : σ={jitter_std:.2f} Hz  (<5 Hz is good)")
    tone_dbfs, residual_dbfs, sinad_db, corr = compute_tone_fit_noise(rec, args.rate, args.tone, args.skip_start)
    print(f"  Tone fit    : {tone_dbfs:.1f} dBFS, corr={corr:.6f}")
    print(f"  Residual    : {residual_dbfs:.1f} dBFS  (noise + distortion after sine fit)")
    print(f"  SINAD       : {sinad_db:.1f} dB  (>60 dB is good for clean audio)")

    make_report(rec, args.rate, args.tone, snr_db, thd, dropouts, inst_freq,
                spectrum, freqs_fft, psd, freqs_psd, args.report)


if __name__ == '__main__':
    main()
