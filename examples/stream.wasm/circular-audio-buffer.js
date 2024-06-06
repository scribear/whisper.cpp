/**
 * Circular serial-in-parallel-out buffer used to store chunks of 32-bit linear PCM audio 
 * normalized to the [-1, 1] range
 */
export class CircularAudioBuffer {
    // Canary value used to determine if the buffer is full
    // Should not appear in valid audio data since 10 > 1
    kCanary = 10;
    constructor(num_chunks, chunk_length) {
        this.chunk_length = chunk_length;
        this.array = new Float32Array(num_chunks * this.chunk_length);
        this.array[this.array.length - 1] = this.kCanary;
        this.offset = 0;
        console.log("internal array: ", this.array);
    }
    /**
     * Push one fixed length chunk into the buffer
     * @param data The Float32Array audio chunk to be pushed into the buffer
     */
    push(data) {
        this.array.set(data, this.offset);
        this.offset = (this.offset + this.chunk_length) % this.array.length;
    }
    /**
     * Return the content of the whole buffer in chronological order
     * @returns A Float32Array containing the content of the buffer
     */
    getAll() {
        // We do not have worry about race condition with push() here, since JS is single-threaded
        let to_return;
        if (this.isFull()) {
            to_return = new Float32Array(this.array.length);
            to_return.set(this.array.slice(this.offset, this.array.length), 0);
            to_return.set(this.array.slice(0, this.offset), this.array.length - this.offset);
        } else {
            to_return = new Float32Array(this.offset);
            to_return.set(this.array.slice(0, this.offset), 0);
        }
        return to_return;
    }
    /**
     * Determines if the buffer is full
     * @returns True if the buffer is full, false else
     */
    isFull() {
        return this.array[this.array.length - 1] != this.kCanary;
    }
    /**
     * Erase all entries from the circular buffer
     */
    clear() {
        this.offset = 0;
        this.array[this.array.length - 1] = this.kCanary;
    }
}