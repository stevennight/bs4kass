#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <expat.h>
#ifndef WIN32
#include <sys/time.h>
#endif

static int insert_new_line = 0;
static int insert_new_text = 0;
static int insert_bracket = 0;
#ifndef WIN32
static struct timeval last_flush = {0};
#endif

static int missing_end = 0;
static char* dialog_buffer = NULL;
static char* dialog_buffer_tail = NULL;

static void XMLCALL
tt_start(void *userData, const char *name, const char **attr)
{
    FILE* fp = (FILE*)userData;

    if (strcmp(name, "div") == 0)
    {
        // Set time
        char begin[] = "0:00:00.00";
        char end[] = "0:00:00.00";
        missing_end = 1;
        for (int i = 0; attr[i]; i += 2)
        {
            if (strcmp(attr[i], "begin") == 0)
            {
                strncpy(begin, attr[i+1]+1, 10);
                if (dialog_buffer != dialog_buffer_tail)
                {
                    strncpy(dialog_buffer+23, attr[i+1]+1, 10);
                    fputs(dialog_buffer, fp);
                }
            }
            if (strcmp(attr[i], "end") == 0)
            {
                strncpy(end, attr[i+1]+1, 10);
                missing_end = 0;
            }
        }
        dialog_buffer_tail = dialog_buffer;
        dialog_buffer_tail += sprintf(dialog_buffer_tail, "Dialogue: 0,%s,%s,Default,,0,0,0,,", begin, end);
#ifndef WIN32
        struct timeval now;
        gettimeofday(&now, NULL);
        if ((now.tv_sec - last_flush.tv_sec) * 1000000 + (now.tv_usec - last_flush.tv_usec) >= 250000)
        {
            printf("%s     \r", begin);
            fflush(stdout);
            last_flush = now;
        }
#else
        printf("%s     \r", begin);
#endif
    }
    else if (strcmp(name, "span") == 0)
    {
        if (insert_new_line) dialog_buffer_tail += sprintf(dialog_buffer_tail, "\\N");
        insert_new_line = 0;
        insert_new_text = 1;
        for (int i = 0; attr[i]; i += 2)
        {
            if (strcmp(attr[i], "style") == 0)
            {
                if (strstr(attr[i+1], "smallSize") != NULL)
                {
                    *(dialog_buffer_tail++) = '<';
                    insert_bracket = 1;
                }
                break;
            }
        }
    }
}

static void XMLCALL
tt_end(void *userData, const char *name)
{
    FILE* fp = (FILE*)userData;

    if (strcmp(name, "div") == 0)
    {
        insert_new_line = 0;
        *(dialog_buffer_tail++) = '\n';
        *(dialog_buffer_tail++) = 0;
        if (!missing_end)
        {
            fputs(dialog_buffer, fp);
            dialog_buffer_tail = dialog_buffer;
        }
    }
    else if (strcmp(name, "p") == 0)
    {
        insert_new_line = 1;
    }
    else if (strcmp(name, "span") == 0)
    {
        if (insert_bracket)
            *(dialog_buffer_tail++) = '>';
        insert_new_text = 0;
        insert_bracket = 0;
    }
}

static void XMLCALL
tt_text(void *userData, const char *s, int len)
{
    if (insert_new_text)
    {
        memcpy(dialog_buffer_tail, s, len);
        dialog_buffer_tail += len;
        *dialog_buffer_tail = 0;
    }
}

void init_ass(FILE* fp)
{
    fputs(
    "[Script Info]\n"
    "; Script generated by BS4KASS\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 1920\n"
    "PlayResY: 1080\n"
    "\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
    "Style: Default,MS UI Gothic,50,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,1,2,10,10,50,0\n"
    "\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    , fp);
}

int main(int argc, char **argv)
{
    const char *filename;
    FILE *ass_file = NULL;

    AVFormatContext *fmt_ctx = NULL;
    int data_stream_idx = -1;
    AVPacket pkt;

    av_log_set_level(AV_LOG_QUIET);
    setvbuf(stdout, NULL, _IOLBF, 128);
    dialog_buffer = (char*)malloc(1048576);
    dialog_buffer_tail = dialog_buffer;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s Source.m2ts\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    size_t len = strlen(filename);
    char* ass_filename = (char*) malloc(len+14);
    strcpy(ass_filename, filename);
    if (strcmp(ass_filename+len-5, ".m2ts") == 0)
        ass_filename[len-5] = 0;
    strcat(ass_filename, ".kingyubi.ass");

    AVDictionary *options = NULL;
    av_dict_set(&options, "analyzeduration", "60000000", 0); // 60 seconds
    av_dict_set(&options, "probesize", "1048576", 0); // 1 GiB

    if (avformat_open_input(&fmt_ctx, filename, NULL, &options) < 0) {
        fprintf(stderr, "Could not open source file %s\n", filename);
        exit(1);
    }
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    data_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_DATA, -1, -1, NULL, 0);
    if (data_stream_idx < 0)
        data_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);
    if (data_stream_idx < 0) {
        fprintf(stderr, "Could not find data stream in input file\n");
        exit(1);
    }

    ass_file = fopen(ass_filename, "wb");
    if (!ass_file) {
        fprintf(stderr, "Could not open destination file %s\n", ass_filename);
        exit(1);
    }

    printf("[Source] %s\n", filename);
    printf("[Target] %s\n", ass_filename);
    printf("[Track#] %d\n", data_stream_idx);
    fflush(stdout);

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    init_ass(ass_file);
    XML_Parser p = NULL;
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == data_stream_idx)
        {
            int last = strncmp(pkt.data + pkt.size - 5, "</tt>", 5) == 0;
            last = last || (strncmp(pkt.data + pkt.size - 7, "</svg>", 6) == 0);
            if (p)
            {
                XML_Parse(p, pkt.data + 1, pkt.size - 1, last);
            }
            else
            {
                p = XML_ParserCreate("UTF-8");

                if (!p) {
                    fprintf(stderr, "Couldn't allocate memory for XML parser\n");
                    exit(1);
                }
                XML_SetElementHandler(p, tt_start, tt_end);
                XML_SetCharacterDataHandler(p, tt_text);
                XML_SetUserData(p, (void*)ass_file);
                XML_Parse(p, pkt.data + 12, pkt.size - 12, last);
            }
            if (last)
            {
                XML_ParserFree(p);
                p = NULL;
            }
            // fputs("\n;", ass_file);
            // fwrite(pkt.data, 1, pkt.size, ass_file);
            // fputs("\n", ass_file);
        }
        av_packet_unref(&pkt);
    }

    if (missing_end)
        fputs(dialog_buffer, ass_file);

    puts("");
    fclose(ass_file);
    avformat_close_input(&fmt_ctx);

    return 0;
}
