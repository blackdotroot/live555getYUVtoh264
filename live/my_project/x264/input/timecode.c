/*****************************************************************************
 * timecode.c: x264 timecode format file input module
 *****************************************************************************
 * Copyright (C) 2010 x264 project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#include "muxers.h"
#include <math.h>

extern cli_input_t input;

typedef struct
{
    cli_input_t input;
    hnd_t p_handle;
    int frame_total;
    int auto_timebase_num;
    int auto_timebase_den;
    uint64_t timebase_num;
    uint64_t timebase_den;
    int seek;
    int stored_pts_num;
    int64_t *pts;
    double assume_fps;
    double last_timecode;
} timecode_hnd_t;

static inline double sigexp10( double value, double *exponent )
{
    /* This function separates significand and exp10 from double floating point. */
    *exponent = pow( 10, floor( log10( value ) ) );
    return value / *exponent;
}

#define DOUBLE_EPSILON 5e-6
#define MKV_TIMEBASE_DEN 1000000000

static double correct_fps( double fps, timecode_hnd_t *h )
{
    int i = 1;
    uint64_t fps_num, fps_den;
    double exponent;
    double fps_sig = sigexp10( fps, &exponent );
    while( 1 )
    {
        fps_den = i * h->timebase_num;
        fps_num = round( fps_den * fps_sig ) * exponent;
        if( fps_num > UINT32_MAX )
        {
            fprintf( stderr, "timecode [error]: tcfile fps correction failed.\n"
                             "                  Specify an appropriate timebase manually or remake tcfile.\n" );
            return -1;
        }
        if( fabs( ((double)fps_num / fps_den) / exponent - fps_sig ) < DOUBLE_EPSILON )
            break;
        ++i;
    }
    if( h->auto_timebase_den )
    {
        h->timebase_den = h->timebase_den ? lcm( h->timebase_den, fps_num ) : fps_num;
        if( h->timebase_den > UINT32_MAX )
            h->auto_timebase_den = 0;
    }
    return (double)fps_num / fps_den;
}

static int try_mkv_timebase_den( double *fpss, timecode_hnd_t *h, int loop_num )
{
    h->timebase_num = 0;
    h->timebase_den = MKV_TIMEBASE_DEN;
    for( int num = 0; num < loop_num; num++ )
    {
        uint64_t fps_den;
        double exponent;
        double fps_sig = sigexp10( fpss[num], &exponent );
        fps_den = round( MKV_TIMEBASE_DEN / fps_sig ) / exponent;
        h->timebase_num = fps_den && h->timebase_num ? gcd( h->timebase_num, fps_den ) : fps_den;
        if( h->timebase_num > UINT32_MAX || !h->timebase_num )
        {
            fprintf( stderr, "timecode [error]: automatic timebase generation failed.\n"
                             "                  Specify timebase manually.\n" );
            return -1;
        }
    }
    return 0;
}

static int parse_tcfile( FILE *tcfile_in, timecode_hnd_t *h, video_info_t *info )
{
    char buff[256];
    int ret, tcfv, num, seq_num, timecodes_num;
    int64_t pts_seek_offset;
    double *timecodes = NULL;
    double *fpss = NULL;

    ret = fscanf( tcfile_in, "# timecode format v%d", &tcfv );
    if( ret != 1 || (tcfv != 1 && tcfv != 2) )
    {
        fprintf( stderr, "timecode [error]: unsupported timecode format\n" );
        return -1;
    }

    if( tcfv == 1 )
    {
        uint64_t file_pos;
        double assume_fps, seq_fps;
        int start, end = h->seek;
        int prev_start = -1, prev_end = -1;

        h->assume_fps = 0;
        for( num = 2; fgets( buff, sizeof(buff), tcfile_in ) != NULL; num++ )
        {
            if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
                continue;
            if( sscanf( buff, "assume %lf", &h->assume_fps ) != 1 && sscanf( buff, "Assume %lf", &h->assume_fps ) != 1 )
            {
                fprintf( stderr, "timecode [error]: tcfile parsing error: assumed fps not found\n" );
                return -1;
            }
            break;
        }
        if( h->assume_fps <= 0 )
        {
            fprintf( stderr, "timecode [error]: invalid assumed fps %.6f\n", h->assume_fps );
            return -1;
        }

        file_pos = ftell( tcfile_in );
        h->stored_pts_num = 0;
        for( seq_num = 0; fgets( buff, sizeof(buff), tcfile_in ) != NULL; num++ )
        {
            if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
            {
                if( sscanf( buff, "# TDecimate Mode 3:  Last Frame = %d", &end ) == 1 )
                    h->stored_pts_num = end + 1 - h->seek;
                continue;
            }
            ret = sscanf( buff, "%d,%d,%lf", &start, &end, &seq_fps );
            if( ret != 3 && ret != EOF )
            {
                fprintf( stderr, "timecode [error]: invalid input tcfile\n" );
                return -1;
            }
            if( start > end || start <= prev_start || end <= prev_end || seq_fps <= 0 )
            {
                fprintf( stderr, "timecode [error]: invalid input tcfile at line %d: %s\n", num, buff );
                return -1;
            }
            prev_start = start;
            prev_end = end;
            if( h->auto_timebase_den || h->auto_timebase_num )
                ++seq_num;
        }
        if( !h->stored_pts_num )
            h->stored_pts_num = end + 1 - h->seek;
        timecodes_num = h->stored_pts_num + h->seek;
        fseek( tcfile_in, file_pos, SEEK_SET );

        timecodes = malloc( timecodes_num * sizeof(double) );
        if( !timecodes )
            return -1;
        if( h->auto_timebase_den || h->auto_timebase_num )
        {
            fpss = malloc( (seq_num + 1) * sizeof(double) );
            if( !fpss )
                goto fail;
        }

        assume_fps = correct_fps( h->assume_fps, h );
        if( assume_fps < 0 )
            goto fail;
        timecodes[0] = 0;
        for( num = seq_num = 0; num < timecodes_num - 1; )
        {
            fgets( buff, sizeof(buff), tcfile_in );
            if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
                continue;
            ret = sscanf( buff, "%d,%d,%lf", &start, &end, &seq_fps );
            if( ret != 3 )
                start = end = timecodes_num - 1;
            for( ; num < start && num < timecodes_num - 1; num++ )
                timecodes[num + 1] = timecodes[num] + 1 / assume_fps;
            if( num < timecodes_num - 1 )
            {
                if( h->auto_timebase_den || h->auto_timebase_num )
                    fpss[seq_num++] = seq_fps;
                seq_fps = correct_fps( seq_fps, h );
                if( seq_fps < 0 )
                    goto fail;
                for( num = start; num <= end && num < timecodes_num - 1; num++ )
                    timecodes[num + 1] = timecodes[num] + 1 / seq_fps;
            }
        }
        if( h->auto_timebase_den || h->auto_timebase_num )
            fpss[seq_num] = h->assume_fps;

        if( h->auto_timebase_num && !h->auto_timebase_den )
        {
            double exponent;
            double assume_fps_sig, seq_fps_sig;
            if( try_mkv_timebase_den( fpss, h, seq_num + 1 ) < 0 )
                goto fail;
            fseek( tcfile_in, file_pos, SEEK_SET );
            assume_fps_sig = sigexp10( h->assume_fps, &exponent );
            assume_fps = MKV_TIMEBASE_DEN / ( round( MKV_TIMEBASE_DEN / assume_fps_sig ) / exponent );
            for( num = 0; num < timecodes_num - 1; )
            {
                fgets( buff, sizeof(buff), tcfile_in );
                if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
                    continue;
                ret = sscanf( buff, "%d,%d,%lf", &start, &end, &seq_fps );
                if( ret != 3 )
                    start = end = timecodes_num - 1;
                seq_fps_sig = sigexp10( seq_fps, &exponent );
                seq_fps = MKV_TIMEBASE_DEN / ( round( MKV_TIMEBASE_DEN / seq_fps_sig ) / exponent );
                for( ; num < start && num < timecodes_num - 1; num++ )
                    timecodes[num + 1] = timecodes[num] + 1 / assume_fps;
                for( num = start; num <= end && num < timecodes_num - 1; num++ )
                    timecodes[num + 1] = timecodes[num] + 1 / seq_fps;
            }
        }
        if( fpss )
            free( fpss );

        h->assume_fps = assume_fps;
        h->last_timecode = timecodes[timecodes_num - 1];
    }
    else    /* tcfv == 2 */
    {
        uint64_t file_pos = ftell( tcfile_in );

        num = h->stored_pts_num = 0;
        while( fgets( buff, sizeof(buff), tcfile_in ) != NULL )
        {
            if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
            {
                if( !num )
                    file_pos = ftell( tcfile_in );
                continue;
            }
            if( num >= h->seek )
                ++h->stored_pts_num;
            ++num;
        }
        timecodes_num = h->stored_pts_num + h->seek;
        if( !timecodes_num )
        {
            fprintf( stderr, "timecode [error]: input tcfile doesn't have any timecodes!\n" );
            return -1;
        }
        fseek( tcfile_in, file_pos, SEEK_SET );

        timecodes = malloc( timecodes_num * sizeof(double) );
        if( !timecodes )
            return -1;

        fgets( buff, sizeof(buff), tcfile_in );
        ret = sscanf( buff, "%lf", &timecodes[0] );
        if( ret != 1 )
        {
            fprintf( stderr, "timecode [error]: invalid input tcfile for frame 0\n" );
            goto fail;
        }
        for( num = 1; num < timecodes_num; )
        {
            fgets( buff, sizeof(buff), tcfile_in );
            if( buff[0] == '#' || buff[0] == '\n' || buff[0] == '\r' )
                continue;
            ret = sscanf( buff, "%lf", &timecodes[num] );
            timecodes[num] *= 1e-3;         /* Timecode format v2 is expressed in milliseconds. */
            if( ret != 1 || timecodes[num] <= timecodes[num - 1] )
            {
                fprintf( stderr, "timecode [error]: invalid input tcfile for frame %d\n", num );
                goto fail;
            }
            ++num;
        }

        if( timecodes_num == 1 )
            h->timebase_den = info->fps_num;
        else if( h->auto_timebase_den )
        {
            fpss = malloc( (timecodes_num - 1) * sizeof(double) );
            if( !fpss )
                goto fail;
            for( num = 0; num < timecodes_num - 1; num++ )
            {
                fpss[num] = 1 / (timecodes[num + 1] - timecodes[num]);
                if( h->timebase_den >= 0 )
                {
                    int i = 1;
                    uint64_t fps_num, fps_den;
                    double exponent;
                    double fps_sig = sigexp10( fpss[num], &exponent );
                    while( 1 )
                    {
                        fps_den = i * h->timebase_num;
                        fps_num = round( fps_den * fps_sig ) * exponent;
                        if( fps_num > UINT32_MAX || fabs( ((double)fps_num / fps_den) / exponent - fps_sig ) < DOUBLE_EPSILON )
                            break;
                        ++i;
                    }
                    h->timebase_den = fps_num && h->timebase_den ? lcm( h->timebase_den, fps_num ) : fps_num;
                    if( h->timebase_den > UINT32_MAX )
                    {
                        h->auto_timebase_den = 0;
                        continue;
                    }
                }
            }
            if( h->auto_timebase_num && !h->auto_timebase_den )
                if( try_mkv_timebase_den( fpss, h, timecodes_num - 1 ) < 0 )
                    goto fail;
            free( fpss );
        }

        if( timecodes_num > 1 )
            h->assume_fps = 1 / (timecodes[timecodes_num - 1] - timecodes[timecodes_num - 2]);
        else
            h->assume_fps = (double)info->fps_num / info->fps_den;
        h->last_timecode = timecodes[timecodes_num - 1];
    }

    if( h->auto_timebase_den || h->auto_timebase_num )
    {
        uint64_t i = gcd( h->timebase_num, h->timebase_den );
        h->timebase_num /= i;
        h->timebase_den /= i;
        fprintf( stderr, "timecode [info]: automatic timebase generation %"PRIu64"/%"PRIu64"\n", h->timebase_num, h->timebase_den );
    }
    else if( h->timebase_den > UINT32_MAX || !h->timebase_den )
    {
        fprintf( stderr, "timecode [error]: automatic timebase generation failed.\n"
                         "                  Specify an appropriate timebase manually.\n" );
        goto fail;
    }

    h->pts = malloc( h->stored_pts_num * sizeof(int64_t) );
    if( !h->pts )
        goto fail;
    pts_seek_offset = (int64_t)( timecodes[h->seek] * ((double)h->timebase_den / h->timebase_num) + 0.5 );
    h->pts[0] = 0;
    for( num = 1; num < h->stored_pts_num; num++ )
    {
        h->pts[num] = (int64_t)( timecodes[h->seek + num] * ((double)h->timebase_den / h->timebase_num) + 0.5 );
        h->pts[num] -= pts_seek_offset;
        if( h->pts[num] <= h->pts[num - 1] )
        {
            fprintf( stderr, "timecode [error]: invalid timebase or timecode for frame %d\n", num );
            goto fail;
        }
    }

    free( timecodes );
    return 0;

fail:
    if( timecodes )
        free( timecodes );
    if( fpss )
        free( fpss );
    return -1;
}

#undef DOUBLE_EPSILON
#undef MKV_TIMEBASE_DEN

static int open_file( char *psz_filename, hnd_t *p_handle, video_info_t *info, cli_input_opt_t *opt )
{
    int ret = 0;
    FILE *tcfile_in;
    timecode_hnd_t *h = malloc( sizeof(timecode_hnd_t) );
    if( !h )
    {
        fprintf( stderr, "timecode [error]: malloc failed\n" );
        return -1;
    }
    h->input = input;
    h->p_handle = *p_handle;
    h->frame_total = input.get_frame_total( h->p_handle );
    h->seek = opt->seek;
    if( opt->timebase )
    {
        ret = sscanf( opt->timebase, "%"SCNu64"/%"SCNu64, &h->timebase_num, &h->timebase_den );
        if( ret == 1 )
            h->timebase_num = strtoul( opt->timebase, NULL, 10 );
        if( h->timebase_num > UINT32_MAX || h->timebase_den > UINT32_MAX )
        {
            fprintf( stderr, "timecode [error]: timebase you specified exceeds H.264 maximum\n" );
            return -1;
        }
    }
    h->auto_timebase_num = !ret;
    h->auto_timebase_den = ret < 2;
    if( h->auto_timebase_num )
        h->timebase_num = info->fps_den; /* can be changed later by auto timebase generation */
    if( h->auto_timebase_den )
        h->timebase_den = 0;             /* set later by auto timebase generation */
    timecode_input.picture_alloc = h->input.picture_alloc;
    timecode_input.picture_clean = h->input.picture_clean;

    *p_handle = h;

    tcfile_in = fopen( psz_filename, "rb" );
    if( !tcfile_in )
    {
        fprintf( stderr, "timecode [error]: can't open `%s'\n", psz_filename );
        return -1;
    }
    else if( !x264_is_regular_file( tcfile_in ) )
    {
        fprintf( stderr, "timecode [error]: tcfile input incompatible with non-regular file `%s'\n", psz_filename );
        fclose( tcfile_in );
        return -1;
    }

    if( parse_tcfile( tcfile_in, h, info ) < 0 )
    {
        if( h->pts )
            free( h->pts );
        fclose( tcfile_in );
        return -1;
    }
    fclose( tcfile_in );

    info->timebase_num = h->timebase_num;
    info->timebase_den = h->timebase_den;
    info->vfr = 1;

    return 0;
}

static int get_frame_total( hnd_t handle )
{
    timecode_hnd_t *h = handle;
    return h->frame_total;
}

static int read_frame( x264_picture_t *p_pic, hnd_t handle, int i_frame )
{
    timecode_hnd_t *h = handle;
    int ret = h->input.read_frame( p_pic, h->p_handle, i_frame );

    if( i_frame - h->seek < h->stored_pts_num )
    {
        assert( i_frame >= h->seek );
        p_pic->i_pts = h->pts[i_frame - h->seek];
    }
    else
    {
        if( h->pts )
        {
            fprintf( stderr, "timecode [info]: input timecode file missing data for frame %d and later\n"
                             "                 assuming constant fps %.6f\n", i_frame, h->assume_fps );
            free( h->pts );
            h->pts = NULL;
        }
        h->last_timecode += 1 / h->assume_fps;
        p_pic->i_pts = (int64_t)( h->last_timecode * ((double)h->timebase_den / h->timebase_num) + 0.5 );
    }

    return ret;
}

static int release_frame( x264_picture_t *pic, hnd_t handle )
{
    timecode_hnd_t *h = handle;
    if( h->input.release_frame )
        return h->input.release_frame( pic, h->p_handle );
    return 0;
}

static int close_file( hnd_t handle )
{
    timecode_hnd_t *h = handle;
    if( h->pts )
        free( h->pts );
    h->input.close_file( h->p_handle );
    free( h );
    return 0;
}

cli_input_t timecode_input = { open_file, get_frame_total, NULL, read_frame, release_frame, NULL, close_file };
