/*
  mkvmerge -- utility for splicing together matroska files
      from component media subtypes

  p_mp3.h

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version \$Id: p_ac3.cpp,v 1.1 2003/02/23 22:51:49 mosu Exp $
    \brief MP3 output module
    \author Moritz Bunkus         <moritz @ bunkus.org>
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "queue.h"
#include "ac3_common.h"
#include "p_ac3.h"

#include "KaxTracks.h"
#include "KaxTrackAudio.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif



ac3_packetizer_c::ac3_packetizer_c(void *nprivate_data, int nprivate_size,
                                   unsigned long nsamples_per_sec,
                                   int nchannels, int nbitrate,
                                   audio_sync_t *nasync, range_t *nrange)
  throw (error_c) : q_c() {
  packetno = 0;
  bytes_output = 0;
  memcpy(&async, nasync, sizeof(audio_sync_t));
  memcpy(&range, nrange, sizeof(range_t));
  packet_buffer = NULL;
  buffer_size = 0;
  set_params(nsamples_per_sec, nchannels, nbitrate);
  set_private_data(nprivate_data, nprivate_size);
  set_header();
}

ac3_packetizer_c::~ac3_packetizer_c() {
  if (packet_buffer != NULL)
    free(packet_buffer);
}

void ac3_packetizer_c::set_params(unsigned long nsamples_per_sec,
                                  int nchannels, int nbitrate) {
  samples_per_sec = nsamples_per_sec;
  channels = nchannels;
  bitrate = nbitrate;
}

void ac3_packetizer_c::add_to_buffer(char *buf, int size) {
  char *new_buffer;
  
  new_buffer = (char *)realloc(packet_buffer, buffer_size + size);
  if (new_buffer == NULL)
    die("realloc");
  
  memcpy(new_buffer + buffer_size, buf, size);
  packet_buffer = new_buffer;
  buffer_size += size;
}

int ac3_packetizer_c::ac3_packet_available() {
  int           pos;
  ac3_header_t  ac3header;
  
  if (packet_buffer == NULL)
    return 0;
  pos = find_ac3_header((unsigned char *)packet_buffer, buffer_size,
                        &ac3header);
  if (pos < 0)
    return 0;
  
  return 1;
}

void ac3_packetizer_c::remove_ac3_packet(int pos, int framesize) {
  int   new_size;
  char *temp_buf;
  
  new_size = buffer_size - (pos + framesize);
  if (new_size != 0) {
    temp_buf = (char *)malloc(new_size);
    if (temp_buf == NULL)
      die("malloc");
    memcpy(temp_buf, &packet_buffer[pos + framesize],
           new_size);
  } else
    temp_buf = NULL;
  free(packet_buffer);
  packet_buffer = temp_buf;
  buffer_size = new_size;
}

char *ac3_packetizer_c::get_ac3_packet(unsigned long *header,
                                       ac3_header_t *ac3header) {
  int     pos;
  char   *buf;
  double  pims;
  
  if (packet_buffer == NULL)
    return 0;
  pos = find_ac3_header((unsigned char *)packet_buffer, buffer_size,
                        ac3header);
  if (pos < 0)
    return 0;
  if ((pos + ac3header->bytes) > buffer_size)
    return 0;

  pims = ((double)ac3header->bytes) * 1000.0 /
         ((double)ac3header->bit_rate / 8.0);

  if (async.displacement < 0) {
    /*
     * AC3 audio synchronization. displacement < 0 means skipping an
     * appropriate number of packets at the beginning.
     */
    async.displacement += (int)pims;
    if (async.displacement > -(pims / 2))
      async.displacement = 0;
    
    remove_ac3_packet(pos, ac3header->bytes);
    
    return 0;
  }

  if (verbose && (pos > 1))
    fprintf(stdout, "ac3_packetizer: skipping %d bytes (no valid AC3 header "
            "found). This might make audio/video go out of sync, but this "
            "stream is damaged.\n", pos);
  buf = (char *)malloc(ac3header->bytes);
  if (buf == NULL)
    die("malloc");
  memcpy(buf, packet_buffer + pos, ac3header->bytes);
  
  if (async.displacement > 0) {
    /*
     * AC3 audio synchronization. displacement > 0 is solved by duplicating
     * the very first AC3 packet as often as necessary. I cannot create
     * a packet with total silence because I don't know how, and simply
     * settings the packet's values to 0 does not work as the AC3 header
     * contains a CRC of its data.
     */
    async.displacement -= (int)pims;
    if (async.displacement < (pims / 2))
      async.displacement = 0;
    
    return buf;
  }

  remove_ac3_packet(pos, ac3header->bytes);
  
  return buf;
}

#define AC3CODECID "A_DOL_AC3"

void ac3_packetizer_c::set_header() {
  using namespace LIBMATROSKA_NAMESPACE;
  
  if (kax_last_entry == NULL)
    track_entry =
      &GetChild<KaxTrackEntry>(static_cast<KaxTracks &>(*kax_tracks));
  else
    track_entry =
      &GetNextChild<KaxTrackEntry>(static_cast<KaxTracks &>(*kax_tracks),
        static_cast<KaxTrackEntry &>(*kax_last_entry));
  kax_last_entry = track_entry;

  serialno = track_number++;
  KaxTrackNumber &tnumber =
    GetChild<KaxTrackNumber>(static_cast<KaxTrackEntry &>(*track_entry));
  *(static_cast<EbmlUInteger *>(&tnumber)) = serialno;
  
  *(static_cast<EbmlUInteger *>
    (&GetChild<KaxTrackType>(static_cast<KaxTrackEntry &>(*track_entry)))) =
      track_audio;

  KaxCodecID &codec_id =
    GetChild<KaxCodecID>(static_cast<KaxTrackEntry &>(*track_entry));
  codec_id.CopyBuffer((binary *)AC3CODECID, countof(AC3CODECID));

  KaxTrackAudio &track_audio =
    GetChild<KaxTrackAudio>(static_cast<KaxTrackEntry &>(*track_entry));

  KaxAudioSamplingFreq &kax_freq = GetChild<KaxAudioSamplingFreq>(track_audio);
  *(static_cast<EbmlFloat *>(&kax_freq)) = (float)samples_per_sec;
  
  KaxAudioChannels &kax_chans = GetChild<KaxAudioChannels>(track_audio);
  *(static_cast<EbmlUInteger *>(&kax_chans)) = channels;
}

int ac3_packetizer_c::process(char *buf, int size, int last_frame) {
  char          *packet;
  unsigned long  header;
  ac3_header_t   ac3header;

  add_to_buffer(buf, size);
  while ((packet = get_ac3_packet(&header, &ac3header)) != NULL) {
    add_packet(packet, ac3header.bytes,
               (u_int64_t)(1000.0 * packetno * 1536 * async.linear / 
               samples_per_sec));
    packetno++;
    free(packet);
  }

  if (last_frame)
    return 0;
  else
    return EMOREDATA;
}

// void ac3_packetizer_c::reset() {
// }
