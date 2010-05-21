// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class is in interface to OmxCodec from the media playback
// pipeline. It interacts with OmxCodec and the VideoDecoderImpl
// in the media pipeline.
//
// THREADING SEMANTICS
//
// This class is created by VideoDecoderImpl and lives on the thread
// that VideoDecoderImpl lives. This class is given the message loop
// for the above thread. The same message loop is used to host
// OmxCodec which is the interface to the actual OpenMAX hardware.
// OmxCodec gurantees that all the callbacks are executed on the
// hosting message loop. This essentially means that all methods in
// this class are executed on the same thread as VideoDecoderImpl.
// Because of that there's no need for locking anywhere.

#include "media/filters/omx_video_decode_engine.h"

#include "base/callback.h"
#include "base/message_loop.h"
#include "media/base/callback.h"
#include "media/ffmpeg/ffmpeg_common.h"

namespace media {

OmxVideoDecodeEngine::OmxVideoDecodeEngine()
    : state_(kCreated){
}

OmxVideoDecodeEngine::~OmxVideoDecodeEngine() {
}

void OmxVideoDecodeEngine::Initialize(
    MessageLoop* message_loop,
    AVStream* av_stream,
    EmptyThisBufferCallback* empty_buffer_callback,
    FillThisBufferCallback* fill_buffer_callback,
    Task* done_cb) {
  fill_this_buffer_callback_.reset(fill_buffer_callback);
  empty_this_buffer_callback_.reset(empty_buffer_callback);

  AutoTaskRunner done_runner(done_cb);
  omx_codec_ = new media::OmxCodec(message_loop);

  AVStream* stream = av_stream;
  width_ = stream->codec->width;
  height_ = stream->codec->height;

  // TODO(ajwong): Find the right way to determine the Omx component name.
  OmxConfigurator::MediaFormat input_format, output_format;
  memset(&input_format, 0, sizeof(input_format));
  memset(&output_format, 0, sizeof(output_format));
  input_format.codec = OmxConfigurator::kCodecH264;
  output_format.codec = OmxConfigurator::kCodecRaw;
  omx_configurator_.reset(
      new OmxDecoderConfigurator(input_format, output_format));
  omx_codec_->Setup(omx_configurator_.get(),
                    NewCallback(this, &OmxVideoDecodeEngine::OnFeedDone),
                    NewCallback(this, &OmxVideoDecodeEngine::OnReadComplete));
  omx_codec_->SetErrorCallback(
      NewCallback(this, &OmxVideoDecodeEngine::OnHardwareError));
  omx_codec_->SetFormatCallback(
      NewCallback(this, &OmxVideoDecodeEngine::OnFormatChange));
  omx_codec_->Start();
  state_ = kNormal;
}

void OmxVideoDecodeEngine::OnFormatChange(
    const OmxConfigurator::MediaFormat& input_format,
    const OmxConfigurator::MediaFormat& output_format) {
  // TODO(jiesun): We should not need this for here, because width and height
  // are already known from upper layer of the stack.
}


void OmxVideoDecodeEngine::OnHardwareError() {
  state_ = kError;
}

void OmxVideoDecodeEngine::EmptyThisBuffer(scoped_refptr<Buffer> buffer) {
  if ( state_ != kNormal ) return; // discard the buffer.
  omx_codec_->Feed(buffer);
}

void OmxVideoDecodeEngine::OnFeedDone(scoped_refptr<Buffer> buffer) {
  empty_this_buffer_callback_->Run(buffer);
}

void OmxVideoDecodeEngine::Flush(Task* done_cb) {
  omx_codec_->Flush(TaskToCallbackAdapter::NewCallback(done_cb));
}

VideoFrame::Format OmxVideoDecodeEngine::GetSurfaceFormat() const {
  return VideoFrame::YV12;
}

void OmxVideoDecodeEngine::Stop(Callback0::Type* done_cb) {
  omx_codec_->Stop(done_cb);

  // TODO(hclam): make sure writing to state_ is safe.
  state_ = kStopped;
}

void OmxVideoDecodeEngine::OnReadComplete(
  OMX_BUFFERHEADERTYPE* buffer) {
  DCHECK_EQ(buffer->nFilledLen, width_*height_*3/2 );

  if (!buffer)  // EOF signal by OmxCodec
    return;

  scoped_refptr<VideoFrame> frame;
  VideoFrame::CreateFrame(GetSurfaceFormat(),
                          width_, height_,
                          StreamSample::kInvalidTimestamp,
                          StreamSample::kInvalidTimestamp,
                          &frame);
  if (!frame.get()) {
    // TODO(jiesun): this is also an error case handled as normal.
    return;
  }

  // TODO(jiesun): Assume YUV 420 format.
  // TODO(jiesun): We will use VideoFrame to wrap OMX_BUFFERHEADTYPE.
  const int pixels = width_ * height_;
  memcpy(frame->data(VideoFrame::kYPlane), buffer->pBuffer, pixels);
  memcpy(frame->data(VideoFrame::kUPlane), buffer->pBuffer + pixels,
         pixels / 4);
  memcpy(frame->data(VideoFrame::kVPlane),
         buffer->pBuffer + pixels + pixels /4,
         pixels / 4);

  fill_this_buffer_callback_->Run(frame);
}

}  // namespace media
