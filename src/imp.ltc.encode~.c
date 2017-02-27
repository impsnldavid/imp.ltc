/**
 ******************************************************************************************************************
 ******************************************************************************************************************
 
 imp.ltc.encode~ V1.0
 by David Butler / The Impersonal Stereo
 
 ******************************************************************************************************************
 
 An ltc_encode object
 
 ******************************************************************************************************************
 
 TEST COPY - NOT FOR DISTRIBUTION
 
 www.theimpersonalstereo.com
 
 ******************************************************************************************************************
 
 */

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include "ltc_ext.h"
#include "decoder.h"
#include "encoder.h"

#include "math.h"


// ******************************************************************************************************************

typedef enum _OutputFormat
{
	OUTPUTFORMAT_RAW,
	OUTPUTFORMAT_REALTIME,
	OUTPUTFORMAT_FRAMES,
	OUTPUTFORMAT_MILLISECONDS
} OutputFormat;


typedef enum _Framerate
{
	FRAMERATE_23_97,
	FRAMERATE_24,
	FRAMERATE_25,
	FRAMERATE_30DF,
	FRAMERATE_30ND,
	FRAMERATE_30
} Framerate;


// Object structure
typedef struct _ltc_encode
{
	t_pxobject object_; // The object
	LTCEncoder* encoder_;


	void* outlet_; // Dump outlet

	char hours_;
	char minutes_;
	char seconds_;
	char frames_;

	double fps_;

	char tclock_;
	char tcReverse_;

	Framerate attrFramerate_;
	OutputFormat attrOutputFormat_;
} t_ltc_encode;


// ******************************************************************************************************************

// Object handing functions
void* ltc_encode_new(t_symbol* s, long argc, t_atom* argv);
void ltc_encode_free(t_ltc_encode* x);
void ltc_encode_assist(t_ltc_encode* x, void* b, long m, long a, char* s);

// Functions
void ltc_encode_dsp64(t_ltc_encode* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags);
void ltc_encode_perform64(t_ltc_encode* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts,
                          long sampleframes, long flags, void* userparam);

t_max_err ltc_encode_attrframerate_set(t_ltc_encode* x, t_object* attr, long argc, t_atom* argv);

// global class pointer variable
static t_class* ltc_encode_class = NULL;

// ******************************************************************************************************************
// 
// MAIN FUNCTION
//
// ******************************************************************************************************************

void ext_main(void* r)
{
	t_class* c;

	// New object method
	c = class_new("imp.ltc.encode~",
	              (method)ltc_encode_new,
	              (method)ltc_encode_free,
	              sizeof(t_ltc_encode),
	              (method)NULL,
	              A_GIMME,
	              0);

	class_dspinit(c);

	// Object methods
	class_addmethod(c, (method)ltc_encode_dsp64, "dsp64", A_CANT, 0);
	class_addmethod(c, (method)ltc_encode_assist, "assist", A_CANT, 0);

	// Object Attributes

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

	post("imp.ltc.decoder~ V1.0 by David Butler / The Impersonal Stereo");

	return 0;
}

// ******************************************************************************************************************
// 
// OBJECT LIFECYCLE
//
// ******************************************************************************************************************

void* ltc_encode_new(t_symbol* s, long argc, t_atom* argv)
{
	t_ltc_encode* x = NULL;

	if ((x = (t_ltc_encode*)object_alloc(ltc_encode_class)))
	{
		dsp_setup((t_pxobject*)x, 1);

		x->outlet_ = outlet_new((t_object*)x, NULL);

		x->hours_ = 0;
		x->minutes_ = 0;
		x->seconds_ = 0;
		x->frames_ = 0;

		x->tclock_ = 0;
		object_attr_setlong(x, gensym("framerate"), FRAMERATE_30DF);
		x->attrOutputFormat_ = OUTPUTFORMAT_RAW;

		attr_args_process(x, argc, argv);

		//TODO : Check these settings
		x->encoder_ = ltc_encoder_create(48000, 25, LTC_TV_525_60, 0);
	}

	return (x);
}

// ******************************************************************************************************************

void ltc_encode_free(t_ltc_encode* x)
{
	dsp_free((t_pxobject*)x);

	ltc_encoder_free(x->encoder_);
}

// ******************************************************************************************************************

void ltc_encode_assist(t_ltc_encode* x, void* b, long m, long a, char* s)
{
	if (m == ASSIST_INLET)
		sprintf(s, "Timecode signal");
	else
		sprintf(s, "Decoded time");
}

// ******************************************************************************************************************
// 
// FUNCTIONS
//
// ******************************************************************************************************************

void ltc_encode_dsp64(t_ltc_encode* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags)
{
	object_method(dsp64, gensym("dsp_add64"), x, ltc_encode_perform64, 0, NULL);
}

// ******************************************************************************************************************


void ltc_encode_perform64(t_ltc_encode* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts,
                          long sampleframes, long flags, void* userparam)

{
}


// ******************************************************************************************************************
//
// ATTRIBUTE FUNCTIONS
//
// ******************************************************************************************************************

t_max_err ltc_encode_attrframerate_set(t_ltc_encode* x, t_object* attr, long argc, t_atom* argv)
{
	x->attrFramerate_ = (Framerate)atom_getlong(argv);

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
