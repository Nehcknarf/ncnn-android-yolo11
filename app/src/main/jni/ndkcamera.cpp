// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "ndkcamera.h"

#include <string>

#include <android/log.h>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "mat.h"

static void onDisconnected(void* context, ACameraDevice* device)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onDisconnected %p", device);
}

static void onError(void* context, ACameraDevice* device, int error)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onError %p %d", device, error);
}

static void onImageAvailable(void* context, AImageReader* reader)
{
//     __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onImageAvailable %p", reader);

    AImage* image = 0;
    media_status_t status = AImageReader_acquireLatestImage(reader, &image);

    if (status != AMEDIA_OK)
    {
        // error
        return;
    }

    int32_t format;
    AImage_getFormat(image, &format);

    // assert format == AIMAGE_FORMAT_YUV_420_888

    int32_t width = 0;
    int32_t height = 0;
    AImage_getWidth(image, &width);
    AImage_getHeight(image, &height);

    int32_t y_pixelStride = 0;
    int32_t u_pixelStride = 0;
    int32_t v_pixelStride = 0;
    AImage_getPlanePixelStride(image, 0, &y_pixelStride);
    AImage_getPlanePixelStride(image, 1, &u_pixelStride);
    AImage_getPlanePixelStride(image, 2, &v_pixelStride);

    int32_t y_rowStride = 0;
    int32_t u_rowStride = 0;
    int32_t v_rowStride = 0;
    AImage_getPlaneRowStride(image, 0, &y_rowStride);
    AImage_getPlaneRowStride(image, 1, &u_rowStride);
    AImage_getPlaneRowStride(image, 2, &v_rowStride);

    uint8_t* y_data = 0;
    uint8_t* u_data = 0;
    uint8_t* v_data = 0;
    int y_len = 0;
    int u_len = 0;
    int v_len = 0;
    AImage_getPlaneData(image, 0, &y_data, &y_len);
    AImage_getPlaneData(image, 1, &u_data, &u_len);
    AImage_getPlaneData(image, 2, &v_data, &v_len);

    if (u_data == v_data + 1 && v_data == y_data + width * height && y_pixelStride == 1 && u_pixelStride == 2 && v_pixelStride == 2 && y_rowStride == width && u_rowStride == width && v_rowStride == width)
    {
        // already nv21  :)
        ((NdkCamera*)context)->on_image((unsigned char*)y_data, (int)width, (int)height);
    }
    else
    {
        // construct nv21
        unsigned char* nv21 = new unsigned char[width * height + width * height / 2];
        {
            // Y
            unsigned char* yptr = nv21;
            for (int y=0; y<height; y++)
            {
                const unsigned char* y_data_ptr = y_data + y_rowStride * y;
                for (int x=0; x<width; x++)
                {
                    yptr[0] = y_data_ptr[0];
                    yptr++;
                    y_data_ptr += y_pixelStride;
                }
            }

            // UV
            unsigned char* uvptr = nv21 + width * height;
            for (int y=0; y<height/2; y++)
            {
                const unsigned char* v_data_ptr = v_data + v_rowStride * y;
                const unsigned char* u_data_ptr = u_data + u_rowStride * y;
                for (int x=0; x<width/2; x++)
                {
                    uvptr[0] = v_data_ptr[0];
                    uvptr[1] = u_data_ptr[0];
                    uvptr += 2;
                    v_data_ptr += v_pixelStride;
                    u_data_ptr += u_pixelStride;
                }
            }
        }

        ((NdkCamera*)context)->on_image((unsigned char*)nv21, (int)width, (int)height);

        delete[] nv21;
    }

    AImage_delete(image);
}

static void onSessionActive(void* context, ACameraCaptureSession *session)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onSessionActive %p", session);
}

static void onSessionReady(void* context, ACameraCaptureSession *session)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onSessionReady %p", session);
}

static void onSessionClosed(void* context, ACameraCaptureSession *session)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onSessionClosed %p", session);
}

void onCaptureFailed(void* context, ACameraCaptureSession* session, ACaptureRequest* request, ACameraCaptureFailure* failure)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onCaptureFailed %p %p %p", session, request, failure);
}

void onCaptureSequenceCompleted(void* context, ACameraCaptureSession* session, int sequenceId, int64_t frameNumber)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onCaptureSequenceCompleted %p %d %ld", session, sequenceId, frameNumber);
}

void onCaptureSequenceAborted(void* context, ACameraCaptureSession* session, int sequenceId)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onCaptureSequenceAborted %p %d", session, sequenceId);
}

void onCaptureCompleted(void* context, ACameraCaptureSession* session, ACaptureRequest* request, const ACameraMetadata* result)
{
//     __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "onCaptureCompleted %p %p %p", session, request, result);
}

NdkCamera::NdkCamera()
{
    camera_facing = 0;
    camera_orientation = 0;

    camera_manager = 0;
    camera_device = 0;
    image_reader = 0;
    image_reader_surface = 0;
    image_reader_target = 0;
    capture_request = 0;
    capture_session_output_container = 0;
    capture_session_output = 0;
    capture_session = 0;


    // setup imagereader and its surface
    {
        AImageReader_new(640, 480, AIMAGE_FORMAT_YUV_420_888, /*maxImages*/2, &image_reader);

        AImageReader_ImageListener listener;
        listener.context = this;
        listener.onImageAvailable = onImageAvailable;

        AImageReader_setImageListener(image_reader, &listener);

        AImageReader_getWindow(image_reader, &image_reader_surface);

        ANativeWindow_acquire(image_reader_surface);
    }
}

NdkCamera::~NdkCamera()
{
    close();

    if (image_reader)
    {
        AImageReader_delete(image_reader);
        image_reader = 0;
    }

    if (image_reader_surface)
    {
        ANativeWindow_release(image_reader_surface);
        image_reader_surface = 0;
    }
}

int NdkCamera::open(int _camera_facing)
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "open");

    camera_facing = _camera_facing;

    camera_manager = ACameraManager_create();

    // find front camera
    std::string camera_id;
    {
        ACameraIdList* camera_id_list = 0;
        ACameraManager_getCameraIdList(camera_manager, &camera_id_list);

        for (int i = 0; i < camera_id_list->numCameras; ++i)
        {
            const char* id = camera_id_list->cameraIds[i];
            ACameraMetadata* camera_metadata = 0;
            ACameraManager_getCameraCharacteristics(camera_manager, id, &camera_metadata);

            // query faceing
            acamera_metadata_enum_android_lens_facing_t facing = ACAMERA_LENS_FACING_FRONT;
            {
                ACameraMetadata_const_entry e = { 0 };
                ACameraMetadata_getConstEntry(camera_metadata, ACAMERA_LENS_FACING, &e);
                facing = (acamera_metadata_enum_android_lens_facing_t)e.data.u8[0];
            }

            if (camera_facing == 0 && facing != ACAMERA_LENS_FACING_FRONT)
            {
                ACameraMetadata_free(camera_metadata);
                continue;
            }

            if (camera_facing == 1 && facing != ACAMERA_LENS_FACING_BACK)
            {
                ACameraMetadata_free(camera_metadata);
                continue;
            }

            camera_id = id;

            // query orientation
            int orientation = 0;
            {
                ACameraMetadata_const_entry e = { 0 };
                ACameraMetadata_getConstEntry(camera_metadata, ACAMERA_SENSOR_ORIENTATION, &e);

                orientation = (int)e.data.i32[0];
            }

//            camera_orientation = orientation;
            camera_orientation = 0;

            ACameraMetadata_free(camera_metadata);

            break;
        }

        ACameraManager_deleteCameraIdList(camera_id_list);
    }

    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "open %s %d", camera_id.c_str(), camera_orientation);

    // open camera
    {
        ACameraDevice_StateCallbacks camera_device_state_callbacks;
        camera_device_state_callbacks.context = this;
        camera_device_state_callbacks.onDisconnected = onDisconnected;
        camera_device_state_callbacks.onError = onError;

        ACameraManager_openCamera(camera_manager, camera_id.c_str(), &camera_device_state_callbacks, &camera_device);
    }

    // capture request
    {
        ACameraDevice_createCaptureRequest(camera_device, TEMPLATE_PREVIEW, &capture_request);

        ACameraOutputTarget_create(image_reader_surface, &image_reader_target);
        ACaptureRequest_addTarget(capture_request, image_reader_target);
    }

    // capture session
    {
        ACameraCaptureSession_stateCallbacks camera_capture_session_state_callbacks;
        camera_capture_session_state_callbacks.context = this;
        camera_capture_session_state_callbacks.onActive = onSessionActive;
        camera_capture_session_state_callbacks.onReady = onSessionReady;
        camera_capture_session_state_callbacks.onClosed = onSessionClosed;

        ACaptureSessionOutputContainer_create(&capture_session_output_container);

        ACaptureSessionOutput_create(image_reader_surface, &capture_session_output);

        ACaptureSessionOutputContainer_add(capture_session_output_container, capture_session_output);

        ACameraDevice_createCaptureSession(camera_device, capture_session_output_container, &camera_capture_session_state_callbacks, &capture_session);

        ACameraCaptureSession_captureCallbacks camera_capture_session_capture_callbacks;
        camera_capture_session_capture_callbacks.context = this;
        camera_capture_session_capture_callbacks.onCaptureStarted = 0;
        camera_capture_session_capture_callbacks.onCaptureProgressed = 0;
        camera_capture_session_capture_callbacks.onCaptureCompleted = onCaptureCompleted;
        camera_capture_session_capture_callbacks.onCaptureFailed = onCaptureFailed;
        camera_capture_session_capture_callbacks.onCaptureSequenceCompleted = onCaptureSequenceCompleted;
        camera_capture_session_capture_callbacks.onCaptureSequenceAborted = onCaptureSequenceAborted;
        camera_capture_session_capture_callbacks.onCaptureBufferLost = 0;

        ACameraCaptureSession_setRepeatingRequest(capture_session, &camera_capture_session_capture_callbacks, 1, &capture_request, nullptr);
    }

    return 0;
}

void NdkCamera::close()
{
    __android_log_print(ANDROID_LOG_WARN, "NdkCamera", "close");

    if (capture_session)
    {
        ACameraCaptureSession_stopRepeating(capture_session);
        ACameraCaptureSession_close(capture_session);
        capture_session = 0;
    }

    if (camera_device)
    {
        ACameraDevice_close(camera_device);
        camera_device = 0;
    }

    if (capture_session_output_container)
    {
        ACaptureSessionOutputContainer_free(capture_session_output_container);
        capture_session_output_container = 0;
    }

    if (capture_session_output)
    {
        ACaptureSessionOutput_free(capture_session_output);
        capture_session_output = 0;
    }

    if (capture_request)
    {
        ACaptureRequest_free(capture_request);
        capture_request = 0;
    }

    if (image_reader_target)
    {
        ACameraOutputTarget_free(image_reader_target);
        image_reader_target = 0;
    }

    if (camera_manager)
    {
        ACameraManager_delete(camera_manager);
        camera_manager = 0;
    }
}

void NdkCamera::on_image(const cv::Mat& rgb) const
{
}

void NdkCamera::on_image(const unsigned char* nv21, int nv21_width, int nv21_height) const
{
    // rotate nv21
    int w = 0;
    int h = 0;
    int rotate_type = 0;
    {
        if (camera_orientation == 0)
        {
            w = nv21_width;
            h = nv21_height;
            rotate_type = camera_facing == 0 ? 2 : 1;
        }
        if (camera_orientation == 90)
        {
            w = nv21_height;
            h = nv21_width;
            rotate_type = camera_facing == 0 ? 5 : 6;
        }
        if (camera_orientation == 180)
        {
            w = nv21_width;
            h = nv21_height;
            rotate_type = camera_facing == 0 ? 4 : 3;
        }
        if (camera_orientation == 270)
        {
            w = nv21_height;
            h = nv21_width;
            rotate_type = camera_facing == 0 ? 7 : 8;
        }
    }

    cv::Mat nv21_rotated(h + h / 2, w, CV_8UC1);
    ncnn::kanna_rotate_yuv420sp(nv21, nv21_width, nv21_height, nv21_rotated.data, w, h, rotate_type);

    // nv21_rotated to rgb
    cv::Mat rgb(h, w, CV_8UC3);
    ncnn::yuv420sp2rgb(nv21_rotated.data, w, h, rgb.data);

    on_image(rgb);
}

static const int NDKCAMERAWINDOW_ID = 233;

NdkCameraWindow::NdkCameraWindow() : NdkCamera()
{
    sensor_manager = 0;
    sensor_event_queue = 0;
    accelerometer_sensor = 0;
    win = 0;

    accelerometer_orientation = 0;

    // sensor
    sensor_manager = ASensorManager_getInstance();

    accelerometer_sensor = ASensorManager_getDefaultSensor(sensor_manager, ASENSOR_TYPE_ACCELEROMETER);

    // 初始化AprilTag检测器
//    tf = tagStandard41h12_create(); // 您可以选择其他标签族
//    td = apriltag_detector_create();
//    apriltag_detector_add_family(td, tf);
//
//    // 可选的AprilTag检测器参数配置:
//     td->quad_decimate = 1.0f; // 图像降采样因子
//     td->nthreads = 4;         // 使用的线程数
//    // td->debug = 0;            // 是否开启调试输出
//    // td->refine_edges = 1;     // 是否细化边缘
//
//    has_last_known_points = false;
    // 可以预设一组固定的点作为最初的 last_known_src_points
    // last_known_src_points.emplace_back(20.0f, 70.0f);
    // last_known_src_points.emplace_back(610.0f, 67.0f);
    // last_known_src_points.emplace_back(480.0f, 402.0f);
    // last_known_src_points.emplace_back(160.0f, 410.0f);
    // has_last_known_points = true; // 如果预设了
}

NdkCameraWindow::~NdkCameraWindow()
{
    if (accelerometer_sensor)
    {
        ASensorEventQueue_disableSensor(sensor_event_queue, accelerometer_sensor);
        accelerometer_sensor = 0;
    }

    if (sensor_event_queue)
    {
        ASensorManager_destroyEventQueue(sensor_manager, sensor_event_queue);
        sensor_event_queue = 0;
    }

    // 释放AprilTag资源
//    if (td) {
//        apriltag_detector_destroy(td);
//        td = nullptr;
//    }
//    if (tf) {
//        // 注意：根据您创建family的函数来选择destroy函数
//        // 例如，如果是 tagStandard41h12_create()，则用 tagStandard41h12_destroy()
//        tagStandard41h12_destroy(tf); // 确保这个函数名和创建时匹配
//        tf = nullptr;
//    }

    if (win)
    {
        ANativeWindow_release(win);
    }
}

void NdkCameraWindow::set_window(ANativeWindow* _win)
{
    if (win)
    {
        ANativeWindow_release(win);
    }

    win = _win;
    ANativeWindow_acquire(win);
}

void NdkCameraWindow::on_image_render(cv::Mat& rgb) const
{
}

void NdkCameraWindow::on_image(const unsigned char* nv21, int nv21_width, int nv21_height) const
{
    // resolve orientation from camera_orientation and accelerometer_sensor
    {
        if (!sensor_event_queue)
        {
            sensor_event_queue = ASensorManager_createEventQueue(sensor_manager, ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS), NDKCAMERAWINDOW_ID, 0, 0);

            ASensorEventQueue_enableSensor(sensor_event_queue, accelerometer_sensor);
        }

        int id = ALooper_pollOnce(0, 0, 0, 0);
        if (id == NDKCAMERAWINDOW_ID)
        {
            ASensorEvent e[8];
            ssize_t num_event = 0;
            while (ASensorEventQueue_hasEvents(sensor_event_queue) == 1)
            {
                num_event = ASensorEventQueue_getEvents(sensor_event_queue, e, 8);
                if (num_event < 0)
                    break;
            }

            if (num_event > 0)
            {
                float acceleration_x = e[num_event - 1].acceleration.x;
                float acceleration_y = e[num_event - 1].acceleration.y;
                float acceleration_z = e[num_event - 1].acceleration.z;
//                 __android_log_print(ANDROID_LOG_WARN, "NdkCameraWindow", "x = %f, y = %f, z = %f", x, y, z);

//                if (acceleration_y > 7)
//                {
//                    accelerometer_orientation = 0;
//                }
//                if (acceleration_x < -7)
//                {
//                    accelerometer_orientation = 90;
//                }
//                if (acceleration_y < -7)
//                {
//                    accelerometer_orientation = 180;
//                }
//                if (acceleration_x > 7)
//                {
//                    accelerometer_orientation = 270;
//                }
            }
        }
    }

    // roi crop and rotate nv21
    int nv21_roi_x = 0;
    int nv21_roi_y = 0;
    int nv21_roi_w = 0;
    int nv21_roi_h = 0;
    int roi_x = 0;
    int roi_y = 0;
    int roi_w = 0;
    int roi_h = 0;
    int rotate_type = 0;
    int render_w = 0;
    int render_h = 0;
    int render_rotate_type = 0;
    {
        int win_w = ANativeWindow_getWidth(win);
        int win_h = ANativeWindow_getHeight(win);

        if (accelerometer_orientation == 90 || accelerometer_orientation == 270)
        {
            std::swap(win_w, win_h);
        }

        const int final_orientation = (camera_orientation + accelerometer_orientation) % 360;

        if (final_orientation == 0 || final_orientation == 180)
        {
            if (win_w * nv21_height > win_h * nv21_width)
            {
                roi_w = nv21_width;
                roi_h = (nv21_width * win_h / win_w) / 2 * 2;
                roi_x = 0;
                roi_y = ((nv21_height - roi_h) / 2) / 2 * 2;
            }
            else
            {
                roi_h = nv21_height;
                roi_w = (nv21_height * win_w / win_h) / 2 * 2;
                roi_x = ((nv21_width - roi_w) / 2) / 2 * 2;
                roi_y = 0;
            }

            nv21_roi_x = roi_x;
            nv21_roi_y = roi_y;
            nv21_roi_w = roi_w;
            nv21_roi_h = roi_h;
        }
        if (final_orientation == 90 || final_orientation == 270)
        {
            if (win_w * nv21_width > win_h * nv21_height)
            {
                roi_w = nv21_height;
                roi_h = (nv21_height * win_h / win_w) / 2 * 2;
                roi_x = 0;
                roi_y = ((nv21_width - roi_h) / 2) / 2 * 2;
            }
            else
            {
                roi_h = nv21_width;
                roi_w = (nv21_width * win_w / win_h) / 2 * 2;
                roi_x = ((nv21_height - roi_w) / 2) / 2 * 2;
                roi_y = 0;
            }

            nv21_roi_x = roi_y;
            nv21_roi_y = roi_x;
            nv21_roi_w = roi_h;
            nv21_roi_h = roi_w;
        }

        if (camera_facing == 0)
        {
            if (camera_orientation == 0 && accelerometer_orientation == 0)
            {
                rotate_type = 2;
            }
            if (camera_orientation == 0 && accelerometer_orientation == 90)
            {
                rotate_type = 7;
            }
            if (camera_orientation == 0 && accelerometer_orientation == 180)
            {
                rotate_type = 4;
            }
            if (camera_orientation == 0 && accelerometer_orientation == 270)
            {
                rotate_type = 5;
            }
            if (camera_orientation == 90 && accelerometer_orientation == 0)
            {
                rotate_type = 5;
            }
            if (camera_orientation == 90 && accelerometer_orientation == 90)
            {
                rotate_type = 2;
            }
            if (camera_orientation == 90 && accelerometer_orientation == 180)
            {
                rotate_type = 7;
            }
            if (camera_orientation == 90 && accelerometer_orientation == 270)
            {
                rotate_type = 4;
            }
            if (camera_orientation == 180 && accelerometer_orientation == 0)
            {
                rotate_type = 4;
            }
            if (camera_orientation == 180 && accelerometer_orientation == 90)
            {
                rotate_type = 5;
            }
            if (camera_orientation == 180 && accelerometer_orientation == 180)
            {
                rotate_type = 2;
            }
            if (camera_orientation == 180 && accelerometer_orientation == 270)
            {
                rotate_type = 7;
            }
            if (camera_orientation == 270 && accelerometer_orientation == 0)
            {
                rotate_type = 7;
            }
            if (camera_orientation == 270 && accelerometer_orientation == 90)
            {
                rotate_type = 4;
            }
            if (camera_orientation == 270 && accelerometer_orientation == 180)
            {
                rotate_type = 5;
            }
            if (camera_orientation == 270 && accelerometer_orientation == 270)
            {
                rotate_type = 2;
            }
        }
        else
        {
            if (final_orientation == 0)
            {
                rotate_type = 1;
            }
            if (final_orientation == 90)
            {
                rotate_type = 6;
            }
            if (final_orientation == 180)
            {
                rotate_type = 3;
            }
            if (final_orientation == 270)
            {
                rotate_type = 8;
            }
        }

        if (accelerometer_orientation == 0)
        {
            render_w = roi_w;
            render_h = roi_h;
            render_rotate_type = 1;
        }
        if (accelerometer_orientation == 90)
        {
            render_w = roi_h;
            render_h = roi_w;
            render_rotate_type = 8;
        }
        if (accelerometer_orientation == 180)
        {
            render_w = roi_w;
            render_h = roi_h;
            render_rotate_type = 3;
        }
        if (accelerometer_orientation == 270)
        {
            render_w = roi_h;
            render_h = roi_w;
            render_rotate_type = 6;
        }
    }

    // crop and rotate nv21
    cv::Mat nv21_croprotated(roi_h + roi_h / 2, roi_w, CV_8UC1);
    {
        const unsigned char* srcY = nv21 + nv21_roi_y * nv21_width + nv21_roi_x;
        unsigned char* dstY = nv21_croprotated.data;
        ncnn::kanna_rotate_c1(srcY, nv21_roi_w, nv21_roi_h, nv21_width, dstY, roi_w, roi_h, roi_w, rotate_type);

        const unsigned char* srcUV = nv21 + nv21_width * nv21_height + nv21_roi_y * nv21_width / 2 + nv21_roi_x;
        unsigned char* dstUV = nv21_croprotated.data + roi_w * roi_h;
        ncnn::kanna_rotate_c2(srcUV, nv21_roi_w / 2, nv21_roi_h / 2, nv21_width, dstUV, roi_w / 2, roi_h / 2, roi_w, rotate_type);
    }

    // nv21_croprotated to rgb
    cv::Mat rgb(roi_h, roi_w, CV_8UC3);
    ncnn::yuv420sp2rgb(nv21_croprotated.data, roi_w, roi_h, rgb.data);
    // 透视变换
    const int output_width = 640;
    const int output_height = 480;
    cv::Size output_size(output_width, output_height);

    std::vector<cv::Point2f> src_points;
    src_points.emplace_back(20.0f, 70.0f); // 左上
    src_points.emplace_back(610.0f, 67.0f); // 右上
    src_points.emplace_back(480.0f,  402.0f); // 右下
    src_points.emplace_back(160.0f,  410.0f); // 左下

    std::vector<cv::Point2f> dst_points;
    dst_points.emplace_back(0.0f, 0.0f); // 左上
    dst_points.emplace_back(output_width, 0.0f); // 右上
    dst_points.emplace_back(output_width, output_height); // 右下
    dst_points.emplace_back(0.0f, output_height); // 左下

    cv::Mat M = cv::getPerspectiveTransform(src_points, dst_points);

    cv::warpPerspective(rgb, rgb, M, output_size, cv::INTER_LINEAR);

//    // --- AprilTag 检测与透视变换 ---
//    const int output_width = 640; // 透视变换后的目标宽度
//    const int output_height = 480; // 透视变换后的目标高度
//    cv::Size output_size(output_width, output_height);
//    std::vector<cv::Point2f> dst_points;
//    dst_points.emplace_back(0.0f, 0.0f);
//    dst_points.emplace_back(output_width, 0.0f);
//    dst_points.emplace_back(output_width, output_height);
//    dst_points.emplace_back(0.0f, output_height);
//
//    std::vector<cv::Point2f> src_points_from_tags(4);
//    bool found_tag0 = false, found_tag1 = false, found_tag2 = false, found_tag3 = false;
//
//    // 1. 将 rgb 图像转为灰度图用于AprilTag检测
//    cv::Mat gray_image;
//    cv::cvtColor(rgb, gray_image, cv::COLOR_RGB2GRAY);
//
//    // 2. 准备 image_u8_t 结构体给 AprilTag 检测器
//    image_u8_t img_header = {
//            .width = gray_image.cols,
//            .height = gray_image.rows,
//            .stride = gray_image.cols, // 对于cv::Mat连续内存的灰度图，stride 通常等于 cols
//            .buf = gray_image.data
//    };
//
//    // 3. 执行 AprilTag 检测
//    // 注意：td 是 NdkCameraWindow 的成员变量，需要用 const_cast 或者将 on_image 声明为非 const
//    // 如果 on_image 必须是 const, 你可能需要将 td 声明为 mutable apriltag_detector_t* td;
//    // 或者将AprilTag检测相关代码移到一个非const辅助函数中。
//    // 这里我们假设 on_image 可以是非 const，或者 td 是 mutable
//    // 为了简单起见，我们假设 td 可以直接在 const 方法中使用（实际中可能需要调整）
//    // 或者更好的做法是移除 on_image 的 const 限定符，因为它修改了内部状态（如 last_known_src_points）
//    // 并调用了非const的AprilTag函数
//
//    // 临时的解决方案，如果on_image必须是const，且td不是mutable:
//    // apriltag_detector_t* current_td = const_cast<NdkCameraWindow*>(this)->td;
//    // zarray_t *detections = apriltag_detector_detect(current_td, &img_header);
//
//    // 更好的方案是移除 on_image 的 const
//    zarray_t *detections = apriltag_detector_detect(td, &img_header);
//
//
//    // 4. 提取角点
//    for (int i = 0; i < zarray_size(detections); i++) {
//        apriltag_detection_t *det;
//        zarray_get(detections, i, &det);
//
//        // 假设AprilTag库角点顺序 p[0]左下, p[1]右下, p[2]右上, p[3]左上 (逆时针)
//        // 您需要根据实际情况调整 det->p[index]
//        if (det->id == 3) { // 左上 Tag ID
//            src_points_from_tags[0] = cv::Point2f(det->p[3][0], det->p[3][1]); // 使用该Tag的左上角
//            found_tag0 = true;
//        } else if (det->id == 2) { // 右上 Tag ID
//            src_points_from_tags[1] = cv::Point2f(det->p[2][0], det->p[2][1]); // 使用该Tag的右上角
//            found_tag1 = true;
//        } else if (det->id == 1) { // 右下 Tag ID
//            src_points_from_tags[2] = cv::Point2f(det->p[1][0], det->p[1][1]); // 使用该Tag的右下角
//            found_tag2 = true;
//        } else if (det->id == 0) { // 左下 Tag ID
//            src_points_from_tags[3] = cv::Point2f(det->p[0][0], det->p[0][1]); // 使用该Tag的左下角
//            found_tag3 = true;
//        }
//        // 可选: 在rgb图像上绘制检测到的Tag，用于调试
////         line(rgb, cv::Point(det->p[0][0], det->p[0][1]), cv::Point(det->p[1][0], det->p[1][1]), cv::Scalar(0,0,255), 2);
////         line(rgb, cv::Point(det->p[1][0], det->p[1][1]), cv::Point(det->p[2][0], det->p[2][1]), cv::Scalar(0,255,0), 2);
////         line(rgb, cv::Point(det->p[2][0], det->p[2][1]), cv::Point(det->p[3][0], det->p[3][1]), cv::Scalar(255,0,0), 2);
////         line(rgb, cv::Point(det->p[3][0], det->p[3][1]), cv::Point(det->p[0][0], det->p[0][1]), cv::Scalar(255,0,255), 2);
//    }
//
//    apriltag_detections_destroy(detections); // 释放检测结果
//
//    std::vector<cv::Point2f> current_src_points;
//    bool use_perspective_transform = false;
//
//    if (found_tag0 && found_tag1 && found_tag2 && found_tag3) {
//        current_src_points = src_points_from_tags;
//        // 如果 on_image 不是 const，可以直接修改成员变量
//        // const_cast<NdkCameraWindow*>(this)->last_known_src_points = src_points_from_tags;
//        // const_cast<NdkCameraWindow*>(this)->has_last_known_points = true;
//        use_perspective_transform = true;
//        __android_log_print(ANDROID_LOG_INFO, "NdkCameraWindow", "All AprilTags found. Using dynamic perspective transform.");
//    } else {
//        // 如果不是所有Tag都被找到，尝试使用上次已知的好点
//        // if (const_cast<NdkCameraWindow*>(this)->has_last_known_points) {
//        //     current_src_points = const_cast<NdkCameraWindow*>(this)->last_known_src_points;
//        //     use_perspective_transform = true;
//        //     __android_log_print(ANDROID_LOG_WARN, "NdkCameraWindow", "Not all AprilTags found. Using last known good points.");
//        // } else {
//        // 或者使用固定的默认点 (您原来的点)
//        current_src_points.clear();
//        current_src_points.emplace_back(20.0f, 70.0f); // 左上
//        current_src_points.emplace_back(610.0f, 67.0f); // 右上
//        current_src_points.emplace_back(480.0f,  402.0f); // 右下
//        current_src_points.emplace_back(160.0f,  410.0f); // 左下
//        use_perspective_transform = true; // 仍然执行变换，但使用固定点
//        __android_log_print(ANDROID_LOG_WARN, "NdkCameraWindow", "Not all AprilTags found. Using fixed default points.");
//        // }
//    }
//
//    if (use_perspective_transform) {
//        cv::Mat M = cv::getPerspectiveTransform(current_src_points, dst_points);
//        cv::warpPerspective(rgb, rgb, M, output_size, cv::INTER_LINEAR); // rgb 图像现在是 output_width x output_height
//    }
//    // --- AprilTag 与透视变换结束 ---

    // 手部检测逻辑
    bool hand_detected_flag = false;
    cv::Mat hsv_image, skin_mask;

    cv::cvtColor(rgb, hsv_image, cv::COLOR_RGB2HSV);

    // 定义肤色的HSV范围 (这些值可能需要根据光照和肤色进行调整)
    cv::Scalar lower_skin_hsv(0, 110, 65);
    cv::Scalar upper_skin_hsv(106, 255, 255);
//    cv::Scalar lower_skin_hsv(0, 5, 100);
//    cv::Scalar upper_skin_hsv(17, 255, 255);
    cv::inRange(hsv_image, lower_skin_hsv, upper_skin_hsv, skin_mask);

    // 查找轮廓
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(skin_mask, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

    // 遍历所有找到的轮廓
    for (size_t i = 0; i < contours.size(); i++)
    {
        double area = cv::contourArea(contours[i]);
        // 设置一个面积阈值来过滤掉小的噪声轮廓
        if (area > 2500) // 假设手的面积（像素）
        {
            hand_detected_flag = true;
            cv::drawContours(rgb, contours, static_cast<int>(i), cv::Scalar(0, 255, 0), 2); // 绿色轮廓表示检测到的手
            break; // 检测到一只手就足够了
        }
    }

    // 根据手部检测结果，条件执行YOLO检测并显示文本
    if (hand_detected_flag)
    {
        cv::putText(rgb, "Hand Detected!", cv::Point(10, 40), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 0, 0), -1);
    }
    else {
        on_image_render(rgb);
    }

    // rotate to native window orientation
    cv::Mat rgb_render(render_h, render_w, CV_8UC3);
    ncnn::kanna_rotate_c3(rgb.data, roi_w, roi_h, rgb_render.data, render_w, render_h, render_rotate_type);

    ANativeWindow_setBuffersGeometry(win, render_w, render_h, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);

    ANativeWindow_Buffer buf;
    ANativeWindow_lock(win, &buf, NULL);

    // scale to target size
    if (buf.format == AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM || buf.format == AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM)
    {
        for (int y = 0; y < render_h; y++)
        {
            const unsigned char* ptr = rgb_render.ptr<const unsigned char>(y);
            unsigned char* outptr = (unsigned char*)buf.bits + buf.stride * 4 * y;

            int x = 0;
#if __ARM_NEON
            for (; x + 7 < render_w; x += 8)
            {
                uint8x8x3_t _rgb = vld3_u8(ptr);
                uint8x8x4_t _rgba;
                _rgba.val[0] = _rgb.val[0];
                _rgba.val[1] = _rgb.val[1];
                _rgba.val[2] = _rgb.val[2];
                _rgba.val[3] = vdup_n_u8(255);
                vst4_u8(outptr, _rgba);

                ptr += 24;
                outptr += 32;
            }
#endif // __ARM_NEON
            for (; x < render_w; x++)
            {
                outptr[0] = ptr[0];
                outptr[1] = ptr[1];
                outptr[2] = ptr[2];
                outptr[3] = 255;

                ptr += 3;
                outptr += 4;
            }
        }
    }

    ANativeWindow_unlockAndPost(win);
}
