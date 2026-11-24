package com.babylonjs.embedding;

import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.media.Image;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import java.io.IOException;
import java.nio.ByteBuffer;

/** Sequential, CPU-readable frames for video texture uploads. MediaPlayer owns the playback clock. */
public final class TextureVideoDecoder {
    private final MediaExtractor extractor = new MediaExtractor();
    private final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
    private MediaCodec codec;
    private boolean inputEnded, outputEnded;
    private long lastRequest = -1, lastOutput = -1;
    private int held = -1;
    private Image image;
    private MediaFormat format;

    public TextureVideoDecoder(AssetManager assets, String path) throws IOException {
        try (AssetFileDescriptor fd = assets.openFd(path)) {
            extractor.setDataSource(fd.getFileDescriptor(), fd.getStartOffset(), fd.getLength());
            for (int track = 0; track < extractor.getTrackCount(); track++) {
                format = extractor.getTrackFormat(track);
                String mime = format.getString(MediaFormat.KEY_MIME);
                if (mime == null || !mime.startsWith("video/")) continue;
                extractor.selectTrack(track);
                format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
                codec = MediaCodec.createDecoderByType(mime);
                codec.configure(format, null, null, 0);
                codec.start();
                return;
            }
            throw new IOException("No video track in " + path);
        } catch (RuntimeException | IOException e) {
            release();
            throw e;
        }
    }

    private void returnBuffer() {
        if (image != null) { image.close(); image = null; }
        if (held >= 0) { codec.releaseOutputBuffer(held, false); held = -1; }
    }

    public Image frameAt(long timeUs) {
        returnBuffer();
        if (timeUs < lastRequest || (lastRequest >= 0 && timeUs - lastRequest > 1_000_000)) {
            extractor.seekTo(timeUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
            codec.flush();
            inputEnded = outputEnded = false;
            lastOutput = -1;
        }
        lastRequest = timeUs;
        if (outputEnded || lastOutput >= timeUs) return null;
        // Bound catch-up work so shutdown and a slow decoder never wait for a whole GOP.
        for (int n = 0; n < 16; n++) {
            int input = inputEnded ? -1 : codec.dequeueInputBuffer(0);
            if (input >= 0) {
                ByteBuffer data = codec.getInputBuffer(input);
                data.clear();
                int size = extractor.readSampleData(data, 0);
                inputEnded = size < 0;
                codec.queueInputBuffer(input, 0, Math.max(size, 0), inputEnded ? 0 : extractor.getSampleTime(),
                        inputEnded ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0);
                if (!inputEnded) extractor.advance();
            }
            int output = codec.dequeueOutputBuffer(info, 1000);
            if (output < 0) continue;
            held = output;
            outputEnded = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
            if (info.size > 0) {
                lastOutput = info.presentationTimeUs;
                if (lastOutput >= timeUs || outputEnded || n == 15) {
                    format = codec.getOutputFormat(output);
                    image = codec.getOutputImage(output);
                    if (image == null) throw new IllegalStateException("Video decoder returned no CPU-readable output image");
                    return image;
                }
            }
            returnBuffer();
            if (outputEnded) break;
        }
        return null;
    }

    public int colorStandard() {
        return format.containsKey(MediaFormat.KEY_COLOR_STANDARD) ? format.getInteger(MediaFormat.KEY_COLOR_STANDARD) : 0;
    }

    public int colorRange() {
        return format.containsKey(MediaFormat.KEY_COLOR_RANGE) ? format.getInteger(MediaFormat.KEY_COLOR_RANGE) : 0;
    }

    public void release() {
        returnBuffer();
        if (codec != null) { codec.release(); codec = null; }
        extractor.release();
    }
}
