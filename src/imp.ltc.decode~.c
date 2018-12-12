// This file is part of imp.ltc
// Copyright (C) 2018 David Butler / The Impersonal Stereo
//
// imp.ltc is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// imp.ltc is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with imp.ltc.
// If not, see <http://www.gnu.org/licenses/>.

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include "ltc_ext.h"
#include "decoder.h"
#include "encoder.h"

#include "math.h"

#include "common.h"



typedef struct _ltc_decode
{
	t_pxobject object_; // The object
	LTCDecoder* decoder_; // The decoder
	LTCFrameExt frame_; // The LTC frame
	ltcsnd_sample_t buffer_[1024]; // The sample buffer

	void* outlet_; // Dump outlet

	char hours_;
	char minutes_;
	char seconds_;
	char frames_;

	double fps_;

	char tclock_;
	char tcReverse_;

	FrameRate attrFramerate_;
	TimecodeFormat attrOutputFormat_;
} t_ltc_decode;



void* ltc_decode_new(t_symbol* s, long argc, t_atom* argv);
void ltc_decode_free(t_ltc_decode* x);
void ltc_decode_assist(t_ltc_decode* x, void* b, long m, long a, char* s);
void ltc_decode_dsp64(t_ltc_decode* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags);
void ltc_decode_perform64(t_ltc_decode* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts,
                          long sampleframes, long flags, void* userparam);
void ltc_decode_tcout(t_ltc_decode* x, t_symbol* s, long argc, t_atom* argv);
int ltc_decode_getnumframes(t_ltc_decode* x);
t_max_err ltc_decode_attrframerate_set(t_ltc_decode* x, t_object* attr, long argc, t_atom* argv);



static t_class* ltc_decode_class = NULL;



void ext_main(void* r)
{
	t_class* c;

	c = class_new("imp.ltc.decode~",
	              (method)ltc_decode_new,
	              (method)ltc_decode_free,
	              sizeof(t_ltc_decode),
	              (method)NULL,
	              A_GIMME,
	              0);

	class_dspinit(c);

	class_addmethod(c, (method)ltc_decode_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)ltc_decode_assist, "assist", A_CANT, 0);

	CLASS_ATTR_CHAR(c, "framerate", 0, t_ltc_decode, attrFramerate_);
	CLASS_ATTR_ENUMINDEX(c, "framerate", 0,
		"\"23.97 FPS\" \"24 FPS\" \"25 FPS\" \"30 Drop-Frame (29.97 FPS)\" \"30 Non-Drop (29.97 FPS)\" \"30 FPS\"");
	CLASS_ATTR_ACCESSORS(c, "framerate", NULL, ltc_decode_attrframerate_set);
	CLASS_ATTR_LABEL(c, "framerate", 0, "Timecode Framerate");
	CLASS_ATTR_SAVE(c, "framerate", 0);

	CLASS_ATTR_CHAR(c, "format", 0, t_ltc_decode, attrOutputFormat_);
	CLASS_ATTR_ENUMINDEX(c, "format", 0, "HH:MM:SS:FF HH:MM:SS.SS \"Total Frames\" \"Total Milliseconds\"");
	CLASS_ATTR_LABEL(c, "format", 0, "Output Format");
	CLASS_ATTR_SAVE(c, "format", 0);

	class_register(CLASS_BOX, c);
	ltc_decode_class = c;

	post("imp.ltc.decode~ V%i.%i.%i by David Butler / The Impersonal Stereo", VERSION_MAJOR, VERSION_MINOR, VERSION_BUGFIX);
}



void* ltc_decode_new(t_symbol* s, long argc, t_atom* argv)
{
	t_ltc_decode* x = NULL;

	if ((x = (t_ltc_decode*)object_alloc(ltc_decode_class)))
	{
		dsp_setup((t_pxobject*)x, 1);

		x->outlet_ = outlet_new((t_object*)x, NULL);

		x->hours_ = 0;
		x->minutes_ = 0;
		x->seconds_ = 0;
		x->frames_ = 0;

		x->tclock_ = 0;
		object_attr_setlong(x, gensym("framerate"), FRAMERATE_30DF);
		x->attrOutputFormat_ = TIMECODEFORMAT_RAW;

		attr_args_process(x, argc, argv);

		x->decoder_ = ltc_decoder_create(1920, 32);
	}

	return x;
}

void ltc_decode_free(t_ltc_decode* x)
{
	dsp_free((t_pxobject*)x);

	ltc_decoder_free(x->decoder_);
}

void ltc_decode_assist(t_ltc_decode* x, void* b, long m, long a, char* s)
{
	if (m == ASSIST_INLET)
		sprintf(s, "Timecode signal");
	else
		sprintf(s, "Decoded time");
}

void ltc_decode_dsp64(t_ltc_decode* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
	object_method(dsp64, gensym("dsp_add64"), x, ltc_decode_perform64, 0, NULL);
}

void ltc_decode_perform64(t_ltc_decode* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts,
                          long sampleframes, long flags, void* userparam)

{
	double* in = ins[0];

	ltc_decoder_write_double(x->decoder_, in, sampleframes, NULL);

	while (ltc_decoder_read(x->decoder_, &x->frame_))
	{
		SMPTETimecode stime;

		ltc_frame_to_time(&stime, &x->frame_.ltc, 1);

		x->hours_ = stime.hours;
		x->minutes_ = stime.mins;
		x->seconds_ = stime.secs;
		x->frames_ = stime.frame;
		x->tcReverse_ = x->frame_.reverse;

		defer((t_object*)x, (method)ltc_decode_tcout, NULL, 0, NULL);
	}
}

void ltc_decode_tcout(t_ltc_decode* x, t_symbol* s, long argc, t_atom* argv)
{
	switch (x->attrOutputFormat_)
	{
		case TIMECODEFORMAT_RAW:
		{
			t_atom av[4];
			atom_setlong(av, x->hours_);
			atom_setlong(av + 1, x->minutes_);
			atom_setlong(av + 2, x->seconds_);
			atom_setlong(av + 3, x->frames_);

			outlet_list(x->outlet_, NULL, 4, av);

			break;
		}
		case TIMECODEFORMAT_REALTIME:
		{
			int totalMilliseconds = floor((ltc_decode_getnumframes(x) * (1000.0f / x->fps_)) + 0.5);

			t_atom_float seconds = floorf(fmod((totalMilliseconds / 1000.0f), 60) * 1000 + 0.5) / 1000;;
			t_atom_long minutes = (t_atom_long)((totalMilliseconds / (1000 * 60)) % 60);
			t_atom_long hours = (t_atom_long)((totalMilliseconds / (1000 * 60 * 60)) % 24);

			t_atom av[3];
			atom_setlong(av, hours);
			atom_setlong(av + 1, minutes);
			atom_setfloat(av + 2, seconds);

			outlet_list(x->outlet_, NULL, 3, av);

			break;
		}
		case TIMECODEFORMAT_FRAMES:
		{
			outlet_int(x->outlet_, ltc_decode_getnumframes(x));
			break;
		}
		case TIMECODEFORMAT_MILLISECONDS:
		{
			outlet_int(x->outlet_, floor((ltc_decode_getnumframes(x) * (1000.0f / x->fps_)) + 0.5));
			break;
		}
	}
}

int ltc_decode_getnumframes(t_ltc_decode* x)
{
	int numFrames = (108000 * x->hours_) + (1800 * x->minutes_) + (30 * x->seconds_) + x->frames_;

	if (x->attrFramerate_ == FRAMERATE_30DF)
	{
		int totalMinutes = 60 * x->hours_ + x->minutes_;
		numFrames -= 2 * (totalMinutes - (totalMinutes / 10));
	}

	return numFrames;
}

t_max_err ltc_decode_attrframerate_set(t_ltc_decode* x, t_object* attr, long argc, t_atom* argv)
{
	x->attrFramerate_ = (FrameRate)atom_getlong(argv);

	switch (x->attrFramerate_)
	{
		case FRAMERATE_23_97:
			x->fps_ = 23.976;
			break;
		case FRAMERATE_24:
			x->fps_ = 24.0;
			break;
		case FRAMERATE_25:
			x->fps_ = 25.0;
			break;
		case FRAMERATE_30DF:
		case FRAMERATE_30ND:
			x->fps_ = 29.97;
			break;
		case FRAMERATE_30:
			x->fps_ = 30.0;
			break;
	}

	return MAX_ERR_NONE;
}
