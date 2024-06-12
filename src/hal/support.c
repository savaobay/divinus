#include "support.h"

void *isp_thread = NULL;
void *venc_thread = NULL;

char chnCount = 0;
hal_chnstate *chnState = NULL;
hal_platform plat = HAL_PLATFORM_UNK;
char series[16] = "unknown";

bool hal_registry(unsigned int addr, unsigned int *data, hal_register_op op) {
    static int mem_fd;
    static char *loaded_area;
    static unsigned int loaded_offset;
    static unsigned int loaded_size;

    unsigned int offset = addr & 0xffff0000;
    unsigned int size = 0xffff;
    if (!addr || (loaded_area && offset != loaded_offset))
        if (munmap(loaded_area, loaded_size))
            fprintf(stderr, "hal_registry munmap error: %s (%d)\n",
                strerror(errno), errno);

    if (!addr) {
        close(mem_fd);
        return true;
    }

    if (!mem_fd && (mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
        fprintf(stderr, "can't open /dev/mem\n");
        return false;
    }

    volatile char *mapped_area;
    if (offset != loaded_offset) {
        mapped_area = mmap64(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, offset);
        if (mapped_area == MAP_FAILED) {
            fprintf(stderr, "hal_registry mmap error: %s (%d)\n",
                    strerror(errno), errno);
            return false;
        }
        loaded_area = (char *)mapped_area;
        loaded_size = size;
        loaded_offset = offset;
    } else
        mapped_area = loaded_area;

    if (op & OP_READ)
        *data = *(volatile unsigned int *)(mapped_area + (addr - offset));
    if (op & OP_WRITE)
        *(volatile unsigned int *)(mapped_area + (addr - offset)) = *data;

    return true;
}

void hal_identify(void) {
    unsigned int val = 0;
    FILE *file;
    char *endMark;
    char line[200] = {0};

#ifdef __arm__
    if (!access("/proc/mi_modules", 0) && 
        hal_registry(0x1F003C00, &val, OP_READ))
        switch (val) {
            case 0xEF: // Macaron (6)
            case 0xF1: // Pudding (6E)
            case 0xF2: // Ispahan (6B0)
                plat = HAL_PLATFORM_I6;
                strcpy(series, val == 0xEF ? "infinity6" :
                    val == 0xF1 ? "infinity6e" :
                    val == 0xF2 ? "infinity6b0" : "unknown");
                chnCount = I6_VENC_CHN_NUM;
                chnState = (hal_chnstate*)i6_state;
                venc_thread = i6_video_thread;
                return;
            case 0xF9:
                plat = HAL_PLATFORM_I6C;
                strcpy(series, "infinity6c");
                chnCount = I6C_VENC_CHN_NUM;
                chnState = (hal_chnstate*)i6c_state;
                venc_thread = i6c_video_thread;
                return;
            case 0xFB:
                plat = HAL_PLATFORM_I6F;
                strcpy(series, "infinity6f");
                chnCount = I6F_VENC_CHN_NUM;
                chnState = (hal_chnstate*)i6f_state;
                venc_thread = i6f_video_thread;
                return;
        }
    
    if (!access("/proc/vcap300", 0)) {
        plat = HAL_PLATFORM_GM;
        strcpy(series, "GM813x");
        if (file = fopen("/proc/pmu/chipver", "r")) {
            fgets(line, 200, file);
            sscanf(line, "%.4s", series + 2);
            fclose(file);
        }
        chnCount = GM_VENC_CHN_NUM;
        chnState = (hal_chnstate*)gm_state;
        venc_thread = gm_video_thread;
        return;
    }
#endif

#ifdef __mips__
    if (!access("/proc/jz", 0) && 
        hal_registry(0x1300002C, &val, OP_READ)) {
        unsigned int type;
        hal_registry(0x13540238, &type, OP_READ);
        char gen = (val >> 12) & 0xFF;
        switch (gen) {
            case 0x31:
                plat = HAL_PLATFORM_T31;
                switch (type >> 16) {
                    case 0x2222: sprintf(series, "T31X");  break;
                    case 0x3333: sprintf(series, "T31L");  break;
                    case 0x4444: sprintf(series, "T31A");  break;
                    case 0x5555: sprintf(series, "T31ZL"); break;
                    case 0x6666: sprintf(series, "T31ZX"); break;
                    case 0xcccc: sprintf(series, "T31AL"); break;
                    case 0xdddd: sprintf(series, "T31ZC"); break;
                    case 0xeeee: sprintf(series, "T31LC"); break;
                    default:     sprintf(series, "T31N");  break;
                }
                chnCount = T31_VENC_CHN_NUM;
                chnState = (hal_chnstate*)t31_state;
                venc_thread = t31_video_thread;
                return;
        }
    }
#endif

#ifdef __arm__
    if (file = fopen("/proc/iomem", "r")) {
        while (fgets(line, 200, file))
            if (strstr(line, "uart")) {
                val = strtol(line, &endMark, 16);
                break;
            }
        fclose(file);
    }

    char v3series = 0;

    switch (val) {
        case 0x12040000:
        case 0x120a0000: val = 0x12020000; break;
        case 0x12100000:
            val = 0x12020000;
            v3series = 1;
            break;
        case 0x12080000: val = 0x12050000; break;
        case 0x20080000: val = 0x20050000; break;

        default: return;
    }

    unsigned SCSYSID[4] = {0};
    for (int i = 0; i < 4; i++) {
        if (!hal_registry(val + 0xEE0 + i * 4, (unsigned*)&SCSYSID[i], OP_READ)) break;
        if (!i && (SCSYSID[i] >> 16 & 0xFF)) { val = SCSYSID[i]; break; }
        val |= (SCSYSID[i] & 0xFF) << i * 8;
    }

    sprintf(series, "%s%X", 
        ((val >> 28) == 0x7) ? "GK" : "Hi", val);
    if (series[6] == '0') {
        series[6] = 'V';
    } else {
        series[8] = series[7];
        series[7] = 'V';
        series[9] = series[8];
        series[10] = series[9];
        series[11] = '\0';
    }

    if (v3series) {
        plat = HAL_PLATFORM_V3;
        chnCount = V3_VENC_CHN_NUM;
        chnState = (hal_chnstate*)v3_state;
        isp_thread = v3_image_thread;
        venc_thread = v3_video_thread;
        return;
    }

    plat = HAL_PLATFORM_V4;
    chnCount = V4_VENC_CHN_NUM;
    chnState = (hal_chnstate*)v4_state;
    isp_thread = v4_image_thread;
    venc_thread = v4_video_thread;
#endif
}
