/*
   Copyright (C) 2001-2006, William Joseph.
   All Rights Reserved.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#if !defined( INCLUDED_CLIPPERTOOL_H )
#define INCLUDED_CLIPPERTOOL_H

#include "generic/vector.h"

class ClipperPoints
{
public:
	Vector3 _points[3];
	ClipperPoints( const Vector3& p0, const Vector3& p1, const Vector3& p2 ){
		_points[0] = p0;
		_points[1] = p1;
		_points[2] = p2;
	}
	const Vector3& operator[]( std::size_t i ) const {
		return _points[i];
	}
	Vector3& operator[]( std::size_t i ){
		return _points[i];
	}
};


void Clipper_setPlanePoints( const ClipperPoints& points );
void Clipper_Construct();
void Clipper_Destroy();
void Clipper_modeChanged( bool isClipper );
bool Clipper_get2pointsIn2d();
void ClipperModeQuick();
void Clipper_doClip();

#endif
