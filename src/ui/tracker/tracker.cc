/****************************************************************************
**
** Copyright (c) 2011 libmv authors.
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
**
****************************************************************************/

#include "ui/tracker/tracker.h"
#include "ui/tracker/scene.h"
#include "ui/tracker/gl.h"

// TODO(MatthiasF): develop and use simple C API
#include "libmv/image/image.h"
#include "libmv/base/vector.h"
#include "libmv/tracking/klt_region_tracker.h"
#include "libmv/tracking/trklt_region_tracker.h"
#include "libmv/tracking/pyramid_region_tracker.h"
#include "libmv/tracking/retrack_region_tracker.h"
using libmv::Marker;
using libmv::vector;

#include <QMouseEvent>
#include <QFileInfo>

// Copy the region starting at x0, y0 with width w, h into region.
// If the region asked for is outside the image border, the marker is removed.
// Returns false if the region leave the image.
bool CopyRegionFromQImage(QImage image,
                          int w, int h,
                          int x0, int y0,
                          libmv::FloatImage *region) {
  Q_ASSERT(image.depth() == 8);
  const unsigned char *data = image.constBits();
  int width = image.width();
  int height = image.height();

  // Return if the region leave the image.
  if (x0 < 0 || y0 < 0 || x0+w >= width || y0+h >= height) return false;

  // Copy the region.
  region->resize(h, w);
  float* dst = region->Data();
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      *dst++ = data[y * width + x];
    }
  }
  return true;
}

Tracker::Tracker(libmv::CameraIntrinsics* intrinsics)
  : QGLWidget(QGLFormat(QGL::SampleBuffers)),
    intrinsics_(intrinsics), scene_(0),
    current_image_(0), active_track_(-1), dragged_(false) {}

void Tracker::Load(QString path) {
  QFile file(path + (QFileInfo(path).isDir()?"/":".") + "tracks");
  if( file.open(QFile::ReadOnly) ) {
    Marker marker;
    while(file.read((char*)&marker,sizeof(Marker))>0) {
      Insert(marker.image, marker.track, marker.x, marker.y);
      // Select all tracks with markers visible on first frame
      if(marker.image==0) selected_tracks_ << marker.track;
    }
  }
  emit trackChanged(selected_tracks_);
}

void Tracker::Save(QString path) {
  QFile file(path + (QFileInfo(path).isDir()?"/":".") + "tracks");
  if (file.open(QFile::WriteOnly | QIODevice::Truncate)) {
    vector<Marker> markers = AllMarkers();
    file.write(reinterpret_cast<char *>(markers.data()),
               markers.size() * sizeof(Marker));
  }
}

void Tracker::SetImage(int id, QImage image) {
  current_image_ = id;
  image_.upload(image);
  upload();
  emit trackChanged(selected_tracks_);
}

void Tracker::SetOverlay(Scene* scene) {
  scene_ = scene;
}

// Track active trackers from the previous image into next one.
void Tracker::Track(int previous, int next, QImage old_image, QImage new_image) {
  // FIXME: the scoped_ptr in Tracking API require the client to heap allocate
  // FIXME: we should wrap those constructors in a C ABI compatible API
  // returning an opaque pointer to be used as first argument for Track
  libmv::TrkltRegionTracker *trklt_region_tracker =
      new libmv::TrkltRegionTracker();
  trklt_region_tracker->half_window_size = kHalfPatternWindowSize;
  trklt_region_tracker->max_iterations = 200;
  libmv::PyramidRegionTracker *pyramid_region_tracker =
      new libmv::PyramidRegionTracker(trklt_region_tracker, kPyramidLevelCount);
  libmv::RetrackRegionTracker region_tracker(pyramid_region_tracker, 0.2);
  vector<Marker> previous_markers = MarkersInImage(previous);
  for (int i = 0; i < previous_markers.size(); i++) {
    const Marker &marker = previous_markers[i];
    if (!selected_tracks_.contains(marker.track)) {
      continue;
    }
    // TODO(keir): For now this uses a fixed size region. What's needed is
    // an extension to use custom sized boxes around the tracked point.
    int half_size = kHalfSearchWindowSize;
    int size = kHalfSearchWindowSize * 2 + 1;

    // TODO(MatthiasF): avoid filtering image tiles twice
    // [xy][01] is the upper right box corner.
    int x0 = marker.x - half_size;
    int y0 = marker.y - half_size;
    libmv::FloatImage old_patch;
    if (!CopyRegionFromQImage(old_image, size, size, x0, y0, &old_patch)) {
      continue;
    }

    int x1 = marker.x - half_size;
    int y1 = marker.y - half_size;
    libmv::FloatImage new_patch;
    if (!CopyRegionFromQImage(new_image, size, size, x1, y1, &new_patch)) {
      continue;
    }

    double xx0 = marker.x - x0;
    double yy0 = marker.y - y0;
    double xx1 = marker.x - x1;
    double yy1 = marker.y - y1;
    region_tracker.Track(old_patch, new_patch, xx0, yy0, &xx1, &yy1);
    Insert(next, marker.track, x1 + xx1, y1 + yy1);
  }
}

void Tracker::select(QVector<int> tracks) {
  selected_tracks_ = tracks;
  upload();
}

void Tracker::deleteSelectedMarkers() {
  foreach (int track, selected_tracks_) {
    RemoveMarker(current_image_, track);
  }
  selected_tracks_.clear();
  upload();
  emit trackChanged(selected_tracks_);
}

void Tracker::deleteSelectedTracks() {
  foreach (int track, selected_tracks_) {
    RemoveMarkersForTrack(track);
  }
  selected_tracks_.clear();
  upload();
  emit trackChanged(selected_tracks_);
}

void Tracker::DrawMarker(const libmv::Marker marker, QVector<vec2> *lines) {
  vec2 center = vec2(marker.x, marker.y);
  vec2 quad[] = { vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, 1) };
  for (int i = 0; i < 4; i++) {
    *lines << center+(kHalfSearchWindowSize+0.5)*quad[i];
    *lines << center+(kHalfSearchWindowSize+0.5)*quad[(i+1)%4];
  }
  for (int i = 0; i < 4; i++) {
    *lines << center+(kHalfPatternWindowSize+0.5)*quad[i];
    *lines << center+(kHalfPatternWindowSize+0.5)*quad[(i+1)%4];
  }
}

bool compare_image(const Marker &a, const Marker &b) {
    return a.image < b.image;
}

void Tracker::upload() {
  makeCurrent();
  vector<Marker> markers = MarkersInImage(current_image_);
  QVector<vec2> lines;
  lines.reserve(markers.size()*8);
  for (int i = 0; i < markers.size(); i++) {
    const Marker &marker = markers[i];
    DrawMarker(marker, &lines);
    vector<Marker> track = MarkersForTrack(marker.track);
    qSort(track.begin(), track.end(), compare_image);
    for (int i = 0; i < track.size()-1; i++) {
      lines << vec2(track[i].x, track[i].y) << vec2(track[i+1].x, track[i+1].y);
    }
  }
  foreach (int track, selected_tracks_) {
    Marker marker = MarkerInImageForTrack(current_image_, track);
    if (marker.image < 0) {
      selected_tracks_.remove(selected_tracks_.indexOf(track));
      continue;
    }
    DrawMarker(marker, &lines);
    DrawMarker(marker, &lines);
    DrawMarker(marker, &lines);
  }
  markers_.primitiveType = 2;
  markers_.upload(lines.constData(), lines.count(), sizeof(vec2));
  update();
}

void Tracker::Render(int x, int y, int w, int h, int image, int track) {
  glBindWindow(x, y, w, h, false);
  glDisableBlend();
  static GLShader image_shader;
  if (!image_shader.id) {
    image_shader.compile(glsl("vertex image"), glsl("fragment image"));
  }
  image_shader.bind();
  image_shader["image"] = 0;
  image_.bind(0);
  float width = 0, height = 0;
  if (image >= 0 && track >= 0) {
    int W = image_.width, H = image_.height;
    Marker marker = MarkerInImageForTrack(image, track);
    vec2 center(marker.x, marker.y);
    vec2 min = (center-kHalfSearchWindowSize) / vec2(W, H);
    vec2 max = (center+kHalfSearchWindowSize) / vec2(W, H);
    glQuad(vec4(-1, 1, min.x, min.y), vec4(1, -1, max.x, max.y));
  } else {
    int W = intrinsics_->image_width(), H = intrinsics_->image_height();
    if (W*h > H*w) {
      width = 1;
      height = static_cast<float>(H*w)/(W*h);
    } else {
      height = 1;
      width = static_cast<float>(W*h)/(H*w);
    }
    glQuad(vec4(-width, -height, 0, 1), vec4(width, height, 1, 0));
    if (scene_ && scene_->isVisible()) scene_->Render(w, h, current_image_);
  }

  static GLShader marker_shader;
  if (!marker_shader.id) {
    marker_shader.compile(glsl("vertex transform marker"),
                          glsl("fragment transform marker"));
  }
  marker_shader.bind();
  mat4 transform;
  if (image >= 0 && track >= 0) {
    Marker marker = MarkerInImageForTrack(image, track);
    vec2 center(marker.x, marker.y);
    vec2 min = center-kHalfSearchWindowSize;
    vec2 max = center+kHalfSearchWindowSize;
    transform.translate(vec3(-1, 1, 0));
    transform.scale(vec3(2.0/(max-min).x, -2.0/(max-min).y, 1));
    transform.translate(vec3(-min.x, -min.y, 0));
  } else {
    int W = image_.width, H = image_.height;
    transform.scale(vec3(2*width/W, -2*height/H, 1));
    transform.translate(vec3(-W/2, -H/2, 0));
    transform_ = transform;
  }
  marker_shader["transform"] = transform;
  markers_.bind();
  markers_.bindAttribute(&marker_shader, "position", 2);
  glAdditiveBlendMode();
  markers_.draw();
}

void Tracker::paintGL() {
  glBindWindow(0, 0, width(), height(),  true);
  Render(0, 0, width(), height());
}

void Tracker::mousePressEvent(QMouseEvent* e) {
  vec2 pos = transform_.inverse()*vec2(2.0*e->x()/width()-1,
                                       1-2.0*e->y()/height());
  last_position_ = pos;
  vector<Marker> markers = MarkersInImage(current_image_);
  for (int i = 0; i < markers.size(); i++) {
    const Marker &marker = markers[i];
    vec2 center = vec2(marker.x, marker.y);
    if (pos > center-kHalfSearchWindowSize && pos < center+kHalfSearchWindowSize) {
      active_track_ = marker.track;
      return;
    }
  }
  int new_track = MaxTrack() + 1;
  Insert(current_image_, new_track, pos.x, pos.y);
  selected_tracks_ << new_track;
  active_track_ = new_track;
  emit trackChanged(selected_tracks_);
  upload();
}

void Tracker::mouseMoveEvent(QMouseEvent* e) {
  vec2 pos = transform_.inverse()*vec2(2.0*e->x()/width()-1,
                                       1-2.0*e->y()/height());
  vec2 delta = pos-last_position_;
  // FIXME: a reference would avoid duplicate lookup
  Marker marker = MarkerInImageForTrack(current_image_, active_track_);
  marker.x += delta.x;
  marker.y += delta.y;
  Insert(current_image_, active_track_, marker.x, marker.y);
  upload();
  last_position_ = pos;
  dragged_ = true;
  emit trackChanged(selected_tracks_);
}

void Tracker::mouseReleaseEvent(QMouseEvent */*event*/) {
  if (!dragged_ && active_track_ >= 0) {
    if (selected_tracks_.contains(active_track_)) {
      selected_tracks_.remove(selected_tracks_.indexOf(active_track_));
    } else {
      selected_tracks_ << active_track_;
    }
    emit trackChanged(selected_tracks_);
  }
  active_track_ = -1;
  dragged_ = false;
  upload();
}

