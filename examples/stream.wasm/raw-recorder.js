/**
 * Audio Worklet that captures raw PCM audio from an input node
 * And passes it as a typed array to the main thread
 */
class RawRecorder extends AudioWorkletProcessor {
    constructor(...args) {
        super(...args);
        this.port.onmessage = (e) => {
          console.log(e.data);
          this.port.postMessage("pong");
        };
      }
    process(inputs, outputs, parameters) {
        // TODO: handle multi-channel input for diarization and stuff?
        // Float32Array containing 128 samples from the first channel of the first input node
        const audio = inputs[0][0];
        // console.log("Worklet: Processed an input chunk");
        this.port.postMessage(audio.buffer, [audio.buffer]);
        return true;
    }
  }
  
  registerProcessor("raw-recorder", RawRecorder);