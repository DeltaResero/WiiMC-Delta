#---------------------------------------------------------------------------------
# FFmpeg for WiiMC: component set and configure options.
# Included by the top-level Makefile, which builds FFmpeg out of tree.
#
# The components are the decoders, demuxers, parsers and bitstream filters
# WiiMC registered with FFmpeg 0.10, under their current names. FFmpeg 9 no
# longer has iff_byterun1 (iff_ilbm covers it), pcm_zork, ffm, the rv30/rv40
# parsers or the mp3_header BSFs; its mpeg1video/mpeg2video decoders replace
# 0.10's generic mpegvideo one. 0.10's h263 decoder also took H.263+, which
# FFmpeg 9 and MPlayer trunk's codec table call h263p. FFmpeg 9 takes
# extradata out of raw streams (VC-1) with the extract_extradata BSF; 0.10's
# parsers did that.
#---------------------------------------------------------------------------------

FFMPEG_DECODERS := \
	amv avs bink ffvhuff flv h261 h263 h263i h263p h264 huffyuv iff_ilbm jpeg2000 \
	jpegls mjpeg mjpegb mpeg1video mpeg2video mpeg4 msmpeg4v1 msmpeg4v2 \
	msmpeg4v3 rawvideo sp5x svq1 svq3 theora thp vc1 vmdvideo vp3 vp5 vp6 vp6a \
	vp6f vp8 wmv1 wmv2 wmv3 wnv1 \
	aac aac_latm ac3 eac3 alac amrnb ape atrac1 atrac3 binkaudio_dct \
	binkaudio_rdft eightsvx_exp eightsvx_fib flac mlp truehd mp1 mp2 mp3 mp3adu \
	mp3on4 mpc7 mpc8 sonic truespeech tta twinvq vmdaudio vorbis wavpack wmapro \
	wmav1 wmav2 wmavoice ws_snd1 \
	adpcm_4xm adpcm_adx adpcm_ct adpcm_ea adpcm_ea_maxis_xa adpcm_ea_r1 \
	adpcm_ea_r2 adpcm_ea_r3 adpcm_ea_xas adpcm_g722 adpcm_g726 adpcm_ima_amv adpcm_ima_apc \
	adpcm_ima_dk3 adpcm_ima_dk4 adpcm_ima_ea_eacs adpcm_ima_ea_sead \
	adpcm_ima_iss adpcm_ima_qt adpcm_ima_smjpeg adpcm_ima_wav adpcm_ima_ws \
	adpcm_ms adpcm_sbpro_2 adpcm_sbpro_3 adpcm_sbpro_4 adpcm_swf adpcm_thp \
	adpcm_thp_le adpcm_xa adpcm_yamaha \
	interplay_dpcm roq_dpcm sol_dpcm xan_dpcm \
	pcm_alaw pcm_bluray pcm_dvd pcm_f32be pcm_f32le pcm_f64be pcm_f64le pcm_lxf \
	pcm_mulaw pcm_s16be pcm_s16be_planar pcm_s16le pcm_s16le_planar pcm_s24be \
	pcm_s24daud pcm_s24le pcm_s32be pcm_s32le pcm_s8 pcm_s8_planar pcm_u16be \
	pcm_u16le pcm_u24be pcm_u24le pcm_u32be pcm_u32le pcm_u8 \
	ass dvbsub dvdsub pgssub srt xsub

FFMPEG_DEMUXERS := \
	aac ac3 adx aea aiff amr anm apc ape asf ass au avi avs bethsoftvid bfi \
	bfstm bink brstm c93 caf cavsvideo cdg daud dfa dirac dnxhd dsicin dts dv \
	dxa ea_cdata ea eac3 ffmetadata filmstrip flac flic flv fourxm g722 gxf h261 \
	h263 h264 idcin iff image2 image2pipe ingenient ipmovie iss iv8 ivf jv lmlm4 \
	lxf m4v matroska mjpeg mlp mm mmf mov mp3 mpc8 mpc mpegps mpegts mpegtsraw \
	mpegvideo msnwc_tcp mtv mvi mxf mxg nc nsv nut nuv ogg oma \
	pcm_alaw pcm_f32be pcm_f32le pcm_f64be pcm_f64le pcm_mulaw pcm_s16be \
	pcm_s16le pcm_s24be pcm_s24le pcm_s32be pcm_s32le pcm_s8 pcm_u16be pcm_u16le \
	pcm_u24be pcm_u24le pcm_u32be pcm_u32le pcm_u8 \
	pva qcp r3d rawvideo rl2 rm roq rpl rso segafilm shorten siff smacker smjpeg \
	sol sox spdif srt str swf thp tiertexseq tmv truehd tta tty txd vc1 vc1t vmd \
	voc vqf w64 wav wsaud wsvqa wtv wv xa xmv xwma yop yuv4mpegpipe

FFMPEG_PARSERS := \
	aac_latm aac ac3 adx cavsvideo dirac dnxhd dvbsub dvdsub flac h261 h263 \
	h264 mjpeg mlp mpeg4video mpegaudio mpegvideo pnm vc1 vp3 vp8

FFMPEG_BSFS := \
	aac_adtstoasc dump_extradata extract_extradata h264_mp4toannexb imx_dump_header mjpeg2jpeg \
	mjpega_dump_header mov2textsub noise remove_extradata text2movsub

# TCP_MAXSEG has libogc's value; its netinet/tcp.h hides it from FFmpeg's
# strict POSIX feature macros.
# WiiMC's changes to FFmpeg, listed in src/lib/README.md
FFMPEG_WII := \
	-DFF_TX_SMALL_SR_TABLES=1 \
	-DFF_H264_8BIT_ONLY=1

FFMPEG_CFLAGS := $(MACHDEP) $(FFMPEG_WII) -I$(LIBOGC_INC) -I$(DEVKITPRO)/portlibs/ppc/include \
	-fno-asynchronous-unwind-tables -DTCP_MAXSEG=2

ffmpeg_comma := ,
ffmpeg_empty :=
ffmpeg_space := $(ffmpeg_empty) $(ffmpeg_empty)
ffmpeg_list = $(subst $(ffmpeg_space),$(ffmpeg_comma),$(strip $(1)))

# --target-os=none: bare metal, newlib and libsysbase provide the libc.
# --disable-dcbzl: dcbzl is a 970 instruction the Wii's 750CL doesn't have.
FFMPEG_CONFIGURE := \
	--prefix=$(FFPREFIX) \
	--enable-cross-compile --cross-prefix=powerpc-eabi- \
	--arch=ppc --cpu=750 --target-os=none \
	--pkg-config=false \
	--enable-static --disable-shared --enable-gpl \
	--disable-programs --disable-doc --disable-avdevice --disable-avfilter \
	--disable-autodetect \
	--disable-pthreads --disable-w32threads --disable-os2threads \
	--disable-runtime-cpudetect --disable-debug \
	--disable-dcbzl \
	--enable-zlib \
	--disable-everything \
	--enable-decoder=$(call ffmpeg_list,$(FFMPEG_DECODERS)) \
	--enable-demuxer=$(call ffmpeg_list,$(FFMPEG_DEMUXERS)) \
	--enable-parser=$(call ffmpeg_list,$(FFMPEG_PARSERS)) \
	--enable-bsf=$(call ffmpeg_list,$(FFMPEG_BSFS)) \
	--enable-swscale --enable-swresample \
	--extra-cflags="$(FFMPEG_CFLAGS)" \
	--extra-ldflags="$(MACHDEP) -L$(LIBOGC_LIB) -L$(DEVKITPRO)/portlibs/ppc/lib" \
	--extra-libs="-logc -lm"
