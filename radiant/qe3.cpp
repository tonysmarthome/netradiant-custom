/*
   Copyright (C) 1999-2006 Id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

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

/*
   The following source code is licensed by Id Software and subject to the terms of
   its LIMITED USE SOFTWARE LICENSE AGREEMENT, a copy of which is included with
   GtkRadiant. If you did not receive a LIMITED USE SOFTWARE LICENSE AGREEMENT,
   please contact Id Software immediately at info@idsoftware.com.
 */

//
// Linux stuff
//
// Leonardo Zide (leo@lokigames.com)
//

#include "qe3.h"

#include "debugging/debugging.h"

#include "ifilesystem.h"
//#include "imap.h"

#include <map>

#include <gtk/gtktearoffmenuitem.h>

#include "stream/textfilestream.h"
#include "cmdlib.h"
#include "stream/stringstream.h"
#include "os/path.h"
#include "scenelib.h"

#include "gtkutil/messagebox.h"
#include "error.h"
#include "map.h"
#include "build.h"
#include "points.h"
#include "camwindow.h"
#include "mainframe.h"
#include "preferences.h"
#include "watchbsp.h"
#include "autosave.h"
#include "convert.h"

QEGlobals_t g_qeglobals;


#if defined( WIN32 )
#define PATH_MAX 260
#endif

#define RADIANT_MONITOR_ADDRESS "127.0.0.1:39000"


void QE_InitVFS(){
	// VFS initialization -----------------------
	// we will call GlobalFileSystem().initDirectory, giving the directories to look in (for files in pk3's and for standalone files)
	// we need to call in order, the mod ones first, then the base ones .. they will be searched in this order
	// *nix systems have a dual filesystem in ~/.q3a, which is searched first .. so we need to add that too

	const char* gamename = gamename_get();
	const char* basegame = basegame_get();
	const char* userRoot = g_qeglobals.m_userEnginePath.c_str();
	const char* globalRoot = EnginePath_get();

	const char* extrapath = ExtraResourcePath_get();
	if( !string_empty( extrapath ) )
		GlobalFileSystem().initDirectory( extrapath );

	// if we have a mod dir
	if ( !string_equal( gamename, basegame ) ) {
		// ~/.<gameprefix>/<fs_game>
		if ( userRoot && !string_equal( globalRoot, userRoot ) ) {
			StringOutputStream userGamePath( 256 );
			userGamePath << userRoot << gamename << '/';
			GlobalFileSystem().initDirectory( userGamePath.c_str() );
		}

		// <fs_basepath>/<fs_game>
		{
			StringOutputStream globalGamePath( 256 );
			globalGamePath << globalRoot << gamename << '/';
			GlobalFileSystem().initDirectory( globalGamePath.c_str() );
		}
	}

	// ~/.<gameprefix>/<fs_main>
	if ( userRoot && !string_equal( globalRoot, userRoot ) ) {
		StringOutputStream userBasePath( 256 );
		userBasePath << userRoot << basegame << '/';
		GlobalFileSystem().initDirectory( userBasePath.c_str() );
	}

	// <fs_basepath>/<fs_main>
	{
		StringOutputStream globalBasePath( 256 );
		globalBasePath << globalRoot << basegame << '/';
		GlobalFileSystem().initDirectory( globalBasePath.c_str() );
	}
}

int g_numbrushes = 0;
int g_numentities = 0;

void QE_UpdateStatusBar(){
	char buffer[128];
	sprintf( buffer, "Brushes: %d Entities: %d", g_numbrushes, g_numentities );
	g_pParentWnd->SetStatusText( g_pParentWnd->m_brushcount_status, buffer );
}

SimpleCounter g_brushCount;

void QE_brushCountChanged(){
	g_numbrushes = int(g_brushCount.get() );
	QE_UpdateStatusBar();
}

SimpleCounter g_entityCount;

void QE_entityCountChanged(){
	g_numentities = int(g_entityCount.get() );
	QE_UpdateStatusBar();
}

bool ConfirmModified( const char* title ){
	if ( !Map_Modified( g_map ) ) {
		return true;
	}

	EMessageBoxReturn result = gtk_MessageBox( GTK_WIDGET( MainFrame_getWindow() ), "The current map has changed since it was last saved.\nDo you want to save the current map before continuing?", title, eMB_YESNOCANCEL, eMB_ICONQUESTION );
	if ( result == eIDCANCEL ) {
		return false;
	}
	if ( result == eIDYES ) {
		if ( Map_Unnamed( g_map ) ) {
			return Map_SaveAs();
		}
		else
		{
			return Map_Save();
		}
	}
	return true;
}

void bsp_init(){
	build_set_variable( "RadiantPath", AppPath_get() );
	build_set_variable( "ExecutableType", RADIANT_EXECUTABLE );
	build_set_variable( "EnginePath", EnginePath_get() );
	build_set_variable( "UserEnginePath", g_qeglobals.m_userEnginePath.c_str() );
	build_set_variable( "MonitorAddress", ( g_WatchBSP_Enabled ) ? RADIANT_MONITOR_ADDRESS : "" );
	build_set_variable( "GameName", gamename_get() );

	const char* mapname = Map_Name( g_map );
	{
		StringOutputStream name( 256 );
		name << StringRange( mapname, path_get_filename_base_end( mapname ) ) << ".bsp";
		build_set_variable( "BspFile", name.c_str() );
	}

	if( g_region_active ){
		StringOutputStream name( 256 );
		name << StringRange( mapname, path_get_filename_base_end( mapname ) ) << ".reg";
		build_set_variable( "MapFile", name.c_str() );
	}
	else{
		build_set_variable( "MapFile", mapname );
	}

	{
		StringOutputStream name( 256 );
		name << StringRange( path_get_filename_start( mapname ), path_get_filename_base_end( mapname ) );
		build_set_variable( "MapName", name.c_str() );
	}
}

void bsp_shutdown(){
	build_clear_variables();
}

class ArrayCommandListener : public CommandListener
{
GPtrArray* m_array;
public:
ArrayCommandListener(){
	m_array = g_ptr_array_new();
}
~ArrayCommandListener(){
	g_ptr_array_free( m_array, TRUE );
}

void execute( const char* command ){
	g_ptr_array_add( m_array, g_strdup( command ) );
}

GPtrArray* array() const {
	return m_array;
}
};

class BatchCommandListener : public CommandListener
{
TextOutputStream& m_file;
std::size_t m_commandCount;
const char* m_outputRedirect;
public:
BatchCommandListener( TextOutputStream& file, const char* outputRedirect ) : m_file( file ), m_commandCount( 0 ), m_outputRedirect( outputRedirect ){
}

void execute( const char* command ){
	m_file << command;
	if( m_outputRedirect ){
		m_file << ( m_commandCount == 0? " > " : " >> " );
		m_file << "\"" << m_outputRedirect << "\"";
	}
	m_file << "\n";
	++m_commandCount;
}
};

bool Region_cameraValid(){
	Vector3 vOrig( vector3_snapped( Camera_getOrigin( *g_pParentWnd->GetCamWnd() ) ) );

	for ( int i = 0 ; i < 3 ; i++ )
	{
		if ( vOrig[i] > g_region_maxs[i] || vOrig[i] < g_region_mins[i] ) {
			return false;
		}
	}
	return true;
}


void RunBSP( const char* name ){
	// http://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=503
	// make sure we don't attempt to region compile a map with the camera outside the region
	if ( g_region_active && !Region_cameraValid() ) {
		globalErrorStream() << "The camera must be in the region to start a region compile.\n";
		return;
	}

	if( !g_region_active )
		SaveMap();

	if ( Map_Unnamed( g_map ) ) {
		globalErrorStream() << "build cancelled: the map is unnamed\n";
		return;
	}

	if ( !g_region_active && g_SnapShots_Enabled && Map_Modified( g_map ) )
		Map_Snapshot();

	if ( g_region_active ) {
		const char* mapname = Map_Name( g_map );
		StringOutputStream name( 256 );
		name << StringRange( mapname, path_get_filename_base_end( mapname ) ) << ".reg";
		Map_SaveRegion( name.c_str() );
	}

	Pointfile_Delete();

	bsp_init();

	ArrayCommandListener listener;
	build_run( name, listener );
	bool monitor = false;
	for ( guint i = 0; i < listener.array()->len; ++i )
		if( strstr( (char*)g_ptr_array_index( listener.array(), i ), RADIANT_MONITOR_ADDRESS ) )
			monitor = true;

	if ( g_WatchBSP_Enabled && monitor ) {
		// grab the file name for engine running
		const char* fullname = Map_Name( g_map );
		StringOutputStream bspname( 64 );
		bspname << StringRange( path_get_filename_start( fullname ), path_get_filename_base_end( fullname ) );
		BuildMonitor_Run( listener.array(), bspname.c_str() );
	}
	else
	{
		char junkpath[PATH_MAX];
		strcpy( junkpath, SettingsPath_get() );
		strcat( junkpath, "junk.txt" );

		char batpath[PATH_MAX];
#if defined( POSIX )
		strcpy( batpath, SettingsPath_get() );
		strcat( batpath, "qe3bsp.sh" );
#elif defined( WIN32 )
		strcpy( batpath, SettingsPath_get() );
		strcat( batpath, "qe3bsp.bat" );
#else
#error "unsupported platform"
#endif
		bool written = false;
		{
			TextFileOutputStream batchFile( batpath );
			if ( !batchFile.failed() ) {
#if defined ( POSIX )
				batchFile << "#!/bin/sh \n\n";
#endif
				BatchCommandListener listener( batchFile, g_WatchBSP0_DumpLog? junkpath : 0 );
				build_run( name, listener );
				written = true;
			}
		}
		if ( written ) {
#if defined ( POSIX )
			chmod( batpath, 0744 );
#endif
			globalOutputStream() << "Writing the compile script to '" << batpath << "'\n";
			if( g_WatchBSP0_DumpLog )
				globalOutputStream() << "The build output will be saved in '" << junkpath << "'\n";
			Q_Exec( batpath, NULL, NULL, true, false );
		}
	}

	bsp_shutdown();
}

// =============================================================================
// Sys_ functions

void Sys_SetTitle( const char *text, bool modified ){
	StringOutputStream title;
	title << text;

	if ( modified ) {
		title << " *";
	}

	gtk_window_set_title( MainFrame_getWindow(), title.c_str() );
}

bool g_bWaitCursor = false;

void Sys_BeginWait( void ){
	ScreenUpdates_Disable( "Processing...", "Please Wait" );
	GdkCursor *cursor = gdk_cursor_new( GDK_WATCH );
	gdk_window_set_cursor( GTK_WIDGET( MainFrame_getWindow() )->window, cursor );
	gdk_cursor_unref( cursor );
	g_bWaitCursor = true;
}

void Sys_EndWait( void ){
	ScreenUpdates_Enable();
	gdk_window_set_cursor( GTK_WIDGET( MainFrame_getWindow() )->window, 0 );
	g_bWaitCursor = false;
}

void Sys_Beep( void ){
	gdk_beep();
}
