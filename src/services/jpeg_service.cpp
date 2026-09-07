#include "jpeg_service.h"

namespace {

JPEGDEC s_decoder;
bool s_busy = false;

} // namespace

JPEGDEC *jpeg_svc_acquire()
{
    if (s_busy) return nullptr;
    s_busy = true;
    return &s_decoder;
}

void jpeg_svc_release(JPEGDEC *decoder)
{
    if (decoder == &s_decoder) s_busy = false;
}

