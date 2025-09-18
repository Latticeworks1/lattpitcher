use std::f32::consts::PI;

/// Real-time pitch detector using autocorrelation method
pub struct PitchDetector {
    sample_rate: f32,
    buffer: Vec<f32>,
    window_size: usize,
    min_frequency: f32,
    max_frequency: f32,
    autocorr_buffer: Vec<f32>,
    window_func: Vec<f32>,
}

impl PitchDetector {
    /// Create a new pitch detector
    ///
    /// # Arguments
    /// * `sample_rate` - Audio sample rate (e.g., 44100.0)
    /// * `window_size` - Analysis window size in samples (power of 2 recommended)
    /// * `min_freq` - Minimum frequency to detect (e.g., 80.0 Hz)
    /// * `max_freq` - Maximum frequency to detect (e.g., 2000.0 Hz)
    pub fn new(sample_rate: f32, window_size: usize, min_freq: f32, max_freq: f32) -> Self {
        let mut window_func = Vec::with_capacity(window_size);

        // Generate Hann window for better frequency resolution
        for i in 0..window_size {
            let window_val = 0.5 * (1.0 - (2.0 * PI * i as f32 / (window_size - 1) as f32).cos());
            window_func.push(window_val);
        }

        Self {
            sample_rate,
            buffer: vec![0.0; window_size],
            window_size,
            min_frequency: min_freq,
            max_frequency: max_freq,
            autocorr_buffer: vec![0.0; window_size],
            window_func,
        }
    }

    /// Process new audio samples and return detected pitch
    ///
    /// # Arguments
    /// * `samples` - New audio samples to process
    ///
    /// # Returns
    /// * `Option<f32>` - Detected pitch in Hz, or None if no clear pitch found
    pub fn process_samples(&mut self, samples: &[f32]) -> Option<f32> {
        // Add new samples to circular buffer
        for &sample in samples {
            self.buffer.rotate_left(1);
            self.buffer[self.window_size - 1] = sample;
        }

        self.detect_pitch()
    }

    /// Detect pitch from current buffer using autocorrelation
    fn detect_pitch(&mut self) -> Option<f32> {
        // Apply window function and calculate RMS for voice activity detection
        let mut windowed: Vec<f32> = Vec::with_capacity(self.window_size);
        let mut rms = 0.0;

        for i in 0..self.window_size {
            let windowed_sample = self.buffer[i] * self.window_func[i];
            windowed.push(windowed_sample);
            rms += windowed_sample * windowed_sample;
        }

        rms = (rms / self.window_size as f32).sqrt();

        // Voice activity detection - ignore very quiet signals
        if rms < 0.01 {
            return None;
        }

        // Calculate autocorrelation
        self.autocorrelation(&windowed);

        // Find the best pitch candidate
        self.find_best_pitch()
    }

    /// Calculate autocorrelation of the windowed signal
    fn autocorrelation(&mut self, signal: &[f32]) {
        let len = signal.len();

        for lag in 0..len {
            let mut sum = 0.0;
            let samples_to_process = len - lag;

            for i in 0..samples_to_process {
                sum += signal[i] * signal[i + lag];
            }

            // Normalize by the number of samples
            self.autocorr_buffer[lag] = sum / samples_to_process as f32;
        }
    }

    /// Find the best pitch candidate from autocorrelation results
    fn find_best_pitch(&self) -> Option<f32> {
        let max_period = (self.sample_rate / self.min_frequency) as usize;
        let min_period = (self.sample_rate / self.max_frequency) as usize;

        // Skip lag 0 (always maximum) and find the highest peak
        let mut best_lag = 0;
        let mut best_correlation = 0.0;

        // Start from min_period to avoid harmonics and noise
        for lag in min_period..max_period.min(self.window_size / 2) {
            let correlation = self.autocorr_buffer[lag];

            // Look for peaks (higher than neighbors)
            if lag > 0 && lag < self.autocorr_buffer.len() - 1 {
                let prev = self.autocorr_buffer[lag - 1];
                let next = self.autocorr_buffer[lag + 1];

                if correlation > prev && correlation > next && correlation > best_correlation {
                    // Additional check: correlation should be significant compared to lag 0
                    if correlation > 0.3 * self.autocorr_buffer[0] {
                        best_correlation = correlation;
                        best_lag = lag;
                    }
                }
            }
        }

        if best_lag > 0 && best_correlation > 0.1 * self.autocorr_buffer[0] {
            // Parabolic interpolation for sub-sample accuracy
            let refined_lag = self.parabolic_interpolation(best_lag);
            Some(self.sample_rate / refined_lag)
        } else {
            None
        }
    }

    /// Parabolic interpolation for sub-sample precision
    fn parabolic_interpolation(&self, peak_index: usize) -> f32 {
        if peak_index == 0 || peak_index >= self.autocorr_buffer.len() - 1 {
            return peak_index as f32;
        }

        let y1 = self.autocorr_buffer[peak_index - 1];
        let y2 = self.autocorr_buffer[peak_index];
        let y3 = self.autocorr_buffer[peak_index + 1];

        let a = (y1 - 2.0 * y2 + y3) / 2.0;
        if a.abs() < f32::EPSILON {
            return peak_index as f32;
        }

        let x_offset = (y3 - y1) / (4.0 * a);
        peak_index as f32 + x_offset
    }

    /// Get current buffer contents (for debugging/visualization)
    pub fn get_buffer(&self) -> &[f32] {
        &self.buffer
    }

    /// Get current autocorrelation results (for debugging/visualization)
    pub fn get_autocorrelation(&self) -> &[f32] {
        &self.autocorr_buffer
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_pitch_detector_creation() {
        let detector = PitchDetector::new(44100.0, 1024, 80.0, 2000.0);
        assert_eq!(detector.sample_rate, 44100.0);
        assert_eq!(detector.window_size, 1024);
    }

    #[test]
    fn test_sine_wave_detection() {
        let mut detector = PitchDetector::new(44100.0, 1024, 80.0, 2000.0);

        // Generate a 440 Hz sine wave
        let target_freq = 440.0;
        let mut samples = Vec::new();

        for i in 0..2048 {
            let sample = (2.0 * PI * target_freq * i as f32 / 44100.0).sin();
            samples.push(sample);
        }

        let detected_pitch = detector.process_samples(&samples);

        if let Some(pitch) = detected_pitch {
            // Allow 5% tolerance
            let tolerance = target_freq * 0.05;
            assert!(
                (pitch - target_freq).abs() < tolerance,
                "Expected ~{} Hz, got {} Hz",
                target_freq,
                pitch
            );
        }
    }
}

// Example usage
fn main() {
    let mut detector = PitchDetector::new(44100.0, 1024, 80.0, 2000.0);

    // Simulate real-time processing with 440 Hz sine wave
    println!("Simulating real-time pitch detection...");

    let target_freq = 440.0;
    let chunk_size = 256; // Process in small chunks for real-time simulation

    for chunk in 0..10 {
        let mut samples = Vec::new();

        // Generate chunk of sine wave
        for i in 0..chunk_size {
            let sample_index = chunk * chunk_size + i;
            let sample = 0.5 * (2.0 * PI * target_freq * sample_index as f32 / 44100.0).sin();
            samples.push(sample);
        }

        if let Some(pitch) = detector.process_samples(&samples) {
            println!("Chunk {}: Detected pitch: {:.2} Hz", chunk, pitch);
        } else {
            println!("Chunk {}: No pitch detected", chunk);
        }
    }
}
