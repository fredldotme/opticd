# opticd

Video-4-Linux bridge using libhybris & v4l2loopback

## Usage

`EGL_PLATFORM=null opticd` in a user session

## Requirements

- v4l2loopback
- [v4l2loopback open/close hint patch](https://gitlab.com/ubports/porting/reference-device-ports/android9/google-pixel-3a/android_kernel_google_bonito/-/commit/cf0c08e3e59147c954fb3c83208ac5b609e8d434)
- [v4l2loopback RGBA32 support](https://gitlab.com/ubports/porting/reference-device-ports/android9/google-pixel-3a/android_kernel_google_bonito/-/commit/17614e7adbe2464aea2832c29a5cdb3fb3b51850)