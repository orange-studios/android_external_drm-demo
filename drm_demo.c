#define _FILE_OFFSET_BITS 64
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
//找到处于连接状态的Connector
drmModeConnector* FindConnector(int fd)
{
    drmModeRes *resources = drmModeGetResources(fd); //drmModeRes描述了计算机所有的显卡信息：connector，encoder，crtc，modes等。
    if (!resources)
    {
        return NULL;
    }

    drmModeConnector* conn = NULL;
    int i = 0;
    for (i = 0; i < resources->count_connectors; i++)
    {
        conn = drmModeGetConnector(fd, resources->connectors[i]);
        if (conn != NULL)
        {
            //找到处于连接状态的Connector。
            if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
            {
                break;
            }
            else
            {
                drmModeFreeConnector(conn);
            }
        }
    }

    drmModeFreeResources(resources);
    return conn;
}

//查找与Connector匹配的Crtc
int FindCrtc(int fd, drmModeConnector *conn)
{
    drmModeEncoder *encoder;
    drmModeRes *resources = drmModeGetResources(fd);
    int crtc_id = 0;
    if (!resources)
    {
        fprintf(stderr, "drmModeGetResources failed\n");
        return -1;
    }

    unsigned int i, j;

//    for (i = 0; i < conn->count_encoders; ++i)
//    {
//        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[i]);
//        if (NULL != enc)
//        {
//            for (j = 0; j < resources->count_crtcs; ++j)
//            {
//                // connector下连接若干encoder，每个encoder支持若干crtc，possible_crtcs的某一位为1代表相应次序（不是id哦）的crtc可用。
//                if ((enc->possible_crtcs & (1 << j)))
//                {
//                    int id = resources->crtcs[j];
//                    drmModeFreeEncoder(enc);
//                    drmModeFreeResources(resources);
//                    return id;
//                }
//            }

//            drmModeFreeEncoder(enc);
//        }
//    }

    for (i = 0; i < resources->count_encoders; i++) {
        encoder = drmModeGetEncoder(fd, resources->encoders[i]);
        if (!encoder) {
            printf("drmModeGetEncoder error");
        }

        if(encoder->crtc_id > 0) {
            break;
        }
        drmModeFreeEncoder(encoder);
    }

    crtc_id = encoder->crtc_id;
    drmModeFreeResources(resources);
    drmModeFreeEncoder(encoder);

    return crtc_id;
}

//绘制一张全色的图
void SetColor(unsigned char *dest, int stride, int w, int h)
{
    struct color {
        unsigned r, g, b;
    };

    struct color ccs[] = {
        { 255, 0, 0 },
        { 0, 255, 0 },
        { 0, 0, 255 },
        { 255, 255, 0 },
        { 0, 255, 255 },
        { 255, 0, 255 }
    };

    static int i = 0;

    unsigned int j, k, off;
    unsigned int r = 255;
    unsigned int g = 1;
    unsigned int b = 1;

    for (j = 0; j < h; ++j)
    {
        for (k = 0; k < w; ++k)
        {
            off = stride * j + k * 4;
            *(uint32_t*)&(dest[off]) = (ccs[i].r << 16) | (ccs[i].g << 8) | ccs[i].b;
        }
    }

    i++;

    printf("draw picture\n");
}

int main(int argc, char *argv[])
{
	int ret, fd;
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
    {
        /* Probably permissions error */
        fprintf(stderr, "couldn't open %s, skipping\n", "");
        return -1;
    }

    drmSetMaster(fd);

    drmModeConnectorPtr connector = FindConnector(fd);
    int width = 800;
    int height = 1280;

    printf("display is %d*%d. connector id = %d \n", width, height, connector->connector_id);

    int crtcid = 12;
    printf("crtcid = %d\n", crtcid);

    struct drm_mode_create_dumb creq;
    memset(&creq, 0, sizeof(creq));

    creq.width = width;
    creq.height = height;
    creq.bpp = 32;
    creq.flags = 0;

    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
    if (ret)
    {
        printf("create dumb failed!\n");
    }

    uint32_t framebuffer = -1;
    uint32_t stride = creq.pitch;

    //使用缓存的handel创建一个FB，返回fb的id：framebuffer。
    ret = drmModeAddFB(fd, width, height, 24, 32, creq.pitch, creq.handle, &framebuffer);

    if (ret)
    {
        printf("failed to create fb\n");
        return -1;
    }

    struct drm_mode_map_dumb mreq; //请求映射缓存到内存。
    mreq.handle = creq.handle;

    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    if (ret)
    {
        printf("map dumb failed!\n");
    }

    // 猜测：创建的缓存位于显存上，在使用之前先使用drm_mode_map_dumb将其映射到内存空间。
    // 但是映射后缓存位于内核内存空间，还需要一次mmap才能被程序使用。
    unsigned char* buf = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mreq.offset);
    if (buf == MAP_FAILED)
    {
        printf("mmap failed!\n");
    }

    memset(buf, 255, creq.size);

    //一切准备完毕，只差连接在一起了！
    ret = drmModeSetCrtc(fd, crtcid, framebuffer, 0, 0, &connector->connector_id, 1, connector->modes);

    if (ret)
    {
        fprintf(stderr, "failed to set mode: %m\n");
        return -1;
    }

    int cc = 0;
    while (cc < 6)
    {
        SetColor(buf, stride, width, height);
        drmModePageFlip(fd, crtcid, framebuffer, DRM_MODE_PAGE_FLIP_EVENT, 0);
        cc++;
        sleep(2);
    }

    printf("over\n");
    getchar();
    close(fd);
    exit(0);
    return ret;
}