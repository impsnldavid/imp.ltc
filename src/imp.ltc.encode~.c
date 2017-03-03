// This file is part of imp.ltc
// Copyright (C) 2017 David Butler / The Impersonal Stereo
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
#include <ext_preprocessor.h>
#include <ext_preprocessor.h>



typedef struct _ltc_encode
{
	t_pxobject object_;
	LTCEncoder* encoder_;

	void* outlet_; // Dump outlet

	char hours_;
	char minutes_;
	char seconds_;
	char frames_;

	double fps_;
	enum LTC_TV_STANDARD tvStandardFlags_;

	char tclock_;
	char tcReverse_;

	Framerate attrFramerate_;
	TimecodeFormat attrOutputFormat_;
} t_ltc_encode;



void* ltc_encode_new(t_symbol* s, long argc, t_atom* argv);
void ltc_encode_free(t_ltc_encode* x);
void ltc_encode_assist(t_ltc_encode* x, void* b, long m, long a, char* s);
void ltc_encode_dsp64(t_ltc_encode* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags);
void ltc_encode_perform64(t_ltc_encode* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts,
                          long sampleframes, long flags, void* userparam);
t_max_err ltc_encode_attrframerate_set(t_ltc_encode* x, t_object* attr, long argc, t_atom* argv);



static t_class* ltc_encode_class = NULL;



void ext_main(void* r)
{
	t_class* c;

	c = class_new("imp.ltc.encode~",
	              (method)ltc_encode_new,
	              (method)ltc_encode_free,
	              sizeof(t_ltc_encode),
	              (method)NULL,
	              A_GIMME,
	              0);

	class_dspinit(c);

	class_addmethod(c, (method)ltc_encode_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)ltc_encode_assist, "assist", A_CANT, 0);

	CLASS_ATTR_CHAR(c, "framerate", 0, t_ltc_encode, attrFramerate_);
	CLASS_ATTR_ENUMINDEX(c, "framerate", 0,
		"\"23.97 FPS\" \"24 FPS\" \"25 FPS\" \"30 Drop-Frame (29.97 FPS)\" \"30 Non-Drop (29.97 FPS)\" \"30 FPS\"");
	CLASS_ATTR_ACCESSORS(c, "framerate", NULL, ltc_encode_attrframerate_set);
	CLASS_ATTR_LABEL(c, "framerate", 0, "Timecode Framerate");
	CLASS_ATTR_SAVE(c, "framerate", 0);

	CLASS_ATTR_CHAR(c, "format", 0, t_ltc_encode, attrOutputFormat_);
	CLASS_ATTR_ENUMINDEX(c, "format", 0, "HH:MM:SS:FF HH:MM:SS.SS \"Total Frames\" \"Total Milliseconds\"");
	CLASS_ATTR_LABEL(c, "format", 0, "Output Format");
	CLASS_ATTR_SAVE(c, "format", 0);

	class_register(CLASS_BOX, c);
	ltc_encode_class = c;

	post("imp.ltc.encode~ V%i.%i.%i by David Butler / The Impersonal Stereo", VERSION_MAJOR, VERSION_MINOR, VERSION_BUGFIX);
}



void* ltc_encode_new(t_symbol* s, long argc, t_atom* argv)
{
	t_ltc_encode* x = NULL;

	if ((x = (t_ltc_encode*)object_alloc(ltc_encode_class)))
	{
		x->outlet_ = outlet_new((t_object*)x, NULL);

		x->hours_ = 0;
		x->minutes_ = 0;
		x->seconds_ = 0;
		x->frames_ = 0;

		x->tclock_ = 0;
		object_attr_setlong(x, gensym("framerate"), FRAMERATE_30DF);
		x->attrOutputFormat_ = TIMECODEFORMAT_RAW;

		attr_args_process(x, argc, argv);

		dsp_setup((t_pxobject*)x, 1);
	}

	return x;
}

void ltc_encode_free(t_ltc_encode* x)
{
	dsp_free((t_pxobject*)x);

	ltc_encoder_free(x->encoder_);
}

void ltc_encode_assist(t_ltc_encode* x, void* b, long m, long a, char* s)
{
	if (m == ASSIST_INLET)
		sprintf(s, "Timecode signal");
	else
		sprintf(s, "Decoded time");
}

void ltc_encode_dsp64(t_ltc_encode* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
	if (x->encoder_ == NULL)
		x->encoder_ = ltc_encoder_create(samplerate, x->fps_, x->tvStandardFlags_, 0);
	else
		ltc_encoder_reinit(x->encoder_, samplerate, x->fps_, x->tvStandardFlags_, 0);

	object_method(dsp64, gensym("dsp_add64"), x, ltc_encode_perform64, 0, NULL);
}

void ltc_encode_perform64(t_ltc_encode* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts,
                          long sampleframes, long flags, void* userparam)

{
}

t_max_err ltc_encode_attrframerate_set(t_ltc_encode* x, t_object* attr, long argc, t_atom* argv)
{
	x->attrFramerate_ = (Framerate)atom_getlong(argv);

	switch (x->attrFramerate_)
	{
		case FRAMERATE_23_97:
			x->fps_ = 23.976;
			x->tvStandardFlags_ = LTC_TV_FILM_24;
			break;
		case FRAMERATE_24:
			x->fps_ = 24.0;
			x->tvStandardFlags_ = LTC_TV_FILM_24;
			break;
		case FRAMERATE_25:
			x->fps_ = 25.0;
			x->tvStandardFlags_ = LTC_TV_625_50;
			break;
		case FRAMERATE_30DF:
		case FRAMERATE_30ND:
			x->fps_ = 29.97;
			x->tvStandardFlags_ = LTC_TV_525_60;
			break;
		case FRAMERATE_30:
			x->fps_ = 30.0;
			x->tvStandardFlags_ = LTC_TV_525_60;
			break;
	}

	return MAX_ERR_NONE;
}
