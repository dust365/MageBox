#pragma once

#include <JPEGDEC.h>

JPEGDEC *jpeg_svc_acquire();
void jpeg_svc_release(JPEGDEC *decoder);

