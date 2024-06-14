#include "sdcard.h"
#include "mp4/mp4.h"
void save_mp4_file(const char *filepath, long duration)
{
    FILE *file = fopen(filepath, "wb");
    if (file == NULL)
    {
        printf("Error opening file!\n");
        return;
    }
    // write mp4 header
    create_header(0);
    fwrite(buf_header.buffer, 1, buf_header.offset, file);
    // write mdat header
    fwrite(buf_mdat.buffer, 1, buf_mdat.offset, file);
    // write moof header
    fwrite(buf_moof.buffer, 1, buf_moof.offset, file);
    // write mdat data
    fwrite(buf_mdat.buffer + buf_mdat.offset, 1, buf_mdat.size - buf_mdat.offset, file);
    // write moof data
    fwrite(buf_moof.buffer + buf_moof.offset, 1, buf_moof.size - buf_moof.offset, file);
    // write moov header
    struct MoovInfo moov_info;
    memset(&moov_info, 0, sizeof(struct MoovInfo));
    moov_info.isH265 = 0;
    moov_info.profile_idc = 100;
    moov_info.level_idc = 41;
    moov_info.width = vid_width;
    moov_info.height = vid_height;
    moov_info.horizontal_resolution = 0x00480000; // 72 dpi
    moov_info.vertical_resolution = 0x00480000;   // 72 dpi
    moov_info.creation_time = 0;
    moov_info.timescale = default_sample_size * vid_framerate;
    moov_info.sps = buf_sps;
    moov_info.sps_length = buf_sps_len;
    moov_info.pps = buf_pps;
    moov_info.pps_length = buf_pps_len;
    moov_info.vps = buf_vps;
    moov_info.vps_length = buf_vps_len;
    create_moov(&moov_info);
    fwrite(buf_header.buffer, 1, buf_header.offset, file);
    fclose(file);
    printf("File saved to %s\n", filepath);
    printf("Duration: %ld\n", duration);
}