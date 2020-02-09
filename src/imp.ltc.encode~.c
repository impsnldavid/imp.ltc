// This file is part of imp.ltc
// Copyright (C) 2020 David Butler / The Impersonal Stereo
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

#include "ltc.h"
#include "decoder.h"
#include "encoder.h"

#include "math.h"

#include "common.h"


typedef struct _ltc_encode
{
	t_pxobject object_;
	LTCEncoder* encoder_;
	t_systhread_mutex mutex_;
	t_bool shouldRefreshEncoder_;

	void* outlet_; // Dump outlet

	char hours_;
	char minutes_;
	char seconds_;
	char frames_;

	double fps_;
	enum LTC_TV_STANDARD tvStandardFlags_;

	char tclock_;
	char tcReverse_;

	FrameRate attrFramerate_;
	TimecodeFormat attrOutputFormat_;
	
	long samplerate_;
	ltcsnd_sample_t* smpteBuffer_;
	int smpteBufferLen_;
	int smpteBufferPos_;

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
		systhread_mutex_new(&x->mutex_, SYSTHREAD_MUTEX_NORMAL);
		x->shouldRefreshEncoder_ = false;
		
		x->outlet_ = outlet_new((t_object*)x, NULL);

		x->smpteBuffer_ = NULL;
		x->smpteBufferLen_ = 0;
		x->smpteBufferPos_ = 0;

		x->hours_ = 0;
		x->minutes_ = 0;
		x->seconds_ = 0;
		x->frames_ = 0;

		x->tclock_ = 0;
		object_attr_setlong(x, gensym("framerate"), FRAMERATE_DEFAULT);
		x->attrOutputFormat_ = TIMECODEFORMAT_RAW;

		attr_args_process(x, argc, argv);

		dsp_setup((t_pxobject*)x, 0);
		outlet_new((t_object *)x, "signal");
		outlet_new((t_object *)x, "signal");
	}

	return x;
}

void ltc_encode_free(t_ltc_encode* x)
{
	dsp_free((t_pxobject*)x);

	if (x->encoder_ != NULL)
		ltc_encoder_free(x->encoder_);

	if (x->smpteBuffer_ != NULL)
		sysmem_freeptr(x->smpteBuffer_);

	systhread_mutex_free(x->mutex_);
}

void ltc_encode_assist(t_ltc_encode* x, void* b, long m, long a, char* s)
{
	if (m == ASSIST_INLET)
	{
		sprintf(s, "imp.ltc.encode~");
	}
	else
	{
		if (a == 0)
			sprintf(s, "(signal) ltc");
		else if (a == 1)
			sprintf(s, "(signal) sync time in ms");
		else
			sprintf(s, "timecode readout");
	}
}

void ltc_encode_dsp64(t_ltc_encode* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
	x->samplerate_ = samplerate;
	x->shouldRefreshEncoder_ = true;

	dsp_add64(dsp64, (t_object*)x, (t_perfroutine64)ltc_encode_perform64, 0, NULL);
}

void ltc_encode_perform64(t_ltc_encode* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags, void* userparam)
{
	double* ltcOut = outs[0];
	double* msOut = outs[1];
	long n = sampleframes;

	if (x->shouldRefreshEncoder_)
	{
		if (systhread_mutex_trylock(x->mutex_) == 0)
		{
			if (x->encoder_ == NULL)
				x->encoder_ = ltc_encoder_create(x->samplerate_, x->fps_, x->tvStandardFlags_, LTC_USE_DATE);

			ltc_encoder_set_buffersize(x->encoder_, x->samplerate_, x->fps_);
			ltc_encoder_reinit(x->encoder_, x->samplerate_, x->fps_, x->tvStandardFlags_, LTC_USE_DATE);

			x->encoder_->f.dfbit = x->attrFramerate_ == FRAMERATE_30DF;

			if (x->smpteBuffer_ != NULL)
				sysmem_freeptr(x->smpteBuffer_);

			x->smpteBuffer_ = (ltcsnd_sample_t*)sysmem_newptrclear(ltc_encoder_get_buffersize(x->encoder_) * sizeof(ltcsnd_sample_t));

			SMPTETimecode st;
			const char timezone[6] = "+0100";
			strcpy(st.timezone, "+0100");
			st.years = 17;
			st.months = 04;
			st.days = 01;

			st.hours = 1;
			st.mins = 0;
			st.secs = 0;
			st.frame = 0;

			ltc_encoder_set_timecode(x->encoder_, &st);
			ltc_encoder_encode_frame(x->encoder_);
            
			x->smpteBufferLen_ = ltc_encoder_copy_buffer(x->encoder_, x->smpteBuffer_);
			x->smpteBufferPos_ = 0;

			x->shouldRefreshEncoder_ = false;

			systhread_mutex_unlock(x->mutex_);
		}	
	}

	while (n--)
	{
		if (x->smpteBufferPos_ >= x->smpteBufferLen_)
		{
			if (systhread_mutex_trylock(x->mutex_) == 0)
			{
				ltc_encoder_inc_timecode(x->encoder_);
				ltc_encoder_encode_frame(x->encoder_);

                x->smpteBufferLen_ = ltc_encoder_get_bufferptr(x->encoder_, &x->smpteBuffer_, 1);

				systhread_mutex_unlock(x->mutex_);

				x->smpteBufferPos_ = 0;
			}
			else
			{
				*ltcOut++ = 0;
				continue;
			}
		}

		*ltcOut++ = x->smpteBuffer_[x->smpteBufferPos_++] / 127.5 - 1.;
	}
}

t_max_err ltc_encode_attrframerate_set(t_ltc_encode* x, t_object* attr, long argc, t_atom* argv)
{
	if (argc > 1)
	{
		object_error((t_object*)x, "Invalid framerate value");
		x->attrFramerate_ = FRAMERATE_DEFAULT;
	}
	else
	{
		switch (atom_gettype(argv))
		{
		case A_LONG:
		{
			t_atom_long value = atom_getlong(argv);

			if (value >= 0 && value <= 5)
			{
				x->attrFramerate_ = (FrameRate)value;
			}
			else
			{
				switch (value)
				{
				case 24:
					x->attrFramerate_ = FRAMERATE_24;
					break;
				case 25:
					x->attrFramerate_ = FRAMERATE_25;
					break;
				case 30:
					x->attrFramerate_ = FRAMERATE_30;
					break;
				default:
					object_error((t_object*)x, "Invalid framerate value '%ld'", value);
					x->attrFramerate_ = FRAMERATE_DEFAULT;
					break;
				}
			}
		}
		break;

		case A_FLOAT:
		{
			t_atom_float value = atom_getfloat(argv);

			if (value == 23.97 || value == (24 / 1.001))
			{
				x->attrFramerate_ = FRAMERATE_23_97;
			}
			else if (value == 29.97 || value == (30 / 1.001))
			{
				x->attrFramerate_ = FRAMERATE_30DF;
			}
			else
			{
				switch ((int)value)
				{
				case 24:
					x->attrFramerate_ = FRAMERATE_24;
					break;
				case 25:
					x->attrFramerate_ = FRAMERATE_25;
					break;
				case 30:
					x->attrFramerate_ = FRAMERATE_30;
					break;
				default:
					object_error((t_object*)x, "Invalid framerate value '%f'", value);
					x->attrFramerate_ = FRAMERATE_DEFAULT;
					break;
				}
			}
		}
		break;

		case A_SYM:
		{
			t_symbol* value = atom_getsym(argv);

			if (strcmp_case_insensitive(value->s_name, "30df") == 0)
			{
				x->attrFramerate_ = FRAMERATE_30DF;
			}
			else if (strcmp_case_insensitive(value->s_name, "30nd") == 0)
			{
				x->attrFramerate_ = FRAMERATE_30ND;
			}
			else
			{
				object_error((t_object*)x, "Invalid framerate value '%s'", value->s_name);
				x->attrFramerate_ = FRAMERATE_DEFAULT;
			}
		}
		break;

		default:
			object_error((t_object*)x, "Invalid framerate value");
			x->attrFramerate_ = FRAMERATE_DEFAULT;
			break;
		}
	}
	

	switch (x->attrFramerate_)
	{
		case FRAMERATE_23_97:
			x->fps_ = 24 / 1.001;
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
			x->fps_ = 30. / 1.001;
			x->tvStandardFlags_ = LTC_TV_525_60;
			break;
		case FRAMERATE_30:
			x->fps_ = 30.0;
			x->tvStandardFlags_ = LTC_TV_525_60;
			break;
	}

	x->shouldRefreshEncoder_ = true;

	return MAX_ERR_NONE;
}
