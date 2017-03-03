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

#ifndef IMPLTC_COMMON_H
#define IMPLTC_COMMON_H 1

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_BUGFIX 0

typedef enum _OutputFormat
{
	TIMECODEFORMAT_RAW,
	TIMECODEFORMAT_REALTIME,
	TIMECODEFORMAT_FRAMES,
	TIMECODEFORMAT_MILLISECONDS
} TimecodeFormat;


typedef enum _Framerate
{
	FRAMERATE_23_97,
	FRAMERATE_24,
	FRAMERATE_25,
	FRAMERATE_30DF,
	FRAMERATE_30ND,
	FRAMERATE_30
} Framerate;


#endif