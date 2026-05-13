# Requires python3, python3-numpy, and python3-scipy

import numpy as np
import scipy.io.wavfile as wavfile

# Sounds to make frequencies
racketfrequency = 932 # Hz (B♭ 5 note)
wallfrequency = 466   # Hz (B♭ 4 note)

# Parameters
duration = 0.025    # seconds
sample_rate = 44100 # Hz sampling rate
amplitude = 1.0     # 0.0 to 1.0, the "volume"

# Generate time axis
t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)

# Generate sine waves
wave1 = amplitude * np.sin(2 * np.pi * racketfrequency * t)
wave2 = amplitude * np.sin(2 * np.pi * wallfrequency * t)

# Save as WAV files
wavfile.write('racket.wav', sample_rate, wave1.astype(np.float32))
wavfile.write('wall.wav', sample_rate, wave2.astype(np.float32))
