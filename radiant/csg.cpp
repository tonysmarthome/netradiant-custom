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

#include "csg.h"

#include "debugging/debugging.h"

#include <list>

#include "map.h"
#include "brushmanip.h"
#include "brushnode.h"
#include "grid.h"
/*
void Face_makeBrush( Face& face, const Brush& brush, brush_vector_t& out, float offset ){
	if ( face.contributes() ) {
		out.push_back( new Brush( brush ) );
		Face* newFace = out.back()->addFace( face );
		face.getPlane().offset( -offset );
		face.planeChanged();
		if ( newFace != 0 ) {
			newFace->flipWinding();
			newFace->getPlane().offset( offset );
			newFace->planeChanged();
		}
	}
}

void Face_extrude( Face& face, const Brush& brush, brush_vector_t& out, float offset ){
	if ( face.contributes() ) {
		face.getPlane().offset( offset );
		out.push_back( new Brush( brush ) );
		face.getPlane().offset( -offset );
		Face* newFace = out.back()->addFace( face );
		if ( newFace != 0 ) {
			newFace->flipWinding();
			newFace->planeChanged();
		}
	}
}
*/
#include "preferences.h"
#include "texwindow.h"
#include "filterbar.h"

typedef std::vector<DoubleVector3> doublevector_vector_t;

enum EHollowType
{
	eDiag = 0,
	eWrap = 1,
	eExtrude = 2,
	ePull = 3,
//	eRoom = 4,
};

class HollowSettings
{
public:
	EHollowType m_hollowType;
	float m_offset;
	Vector3 m_exclusionAxis;
	double m_mindot;
	double m_maxdot;
	bool m_caulk;
	bool m_removeInner;
};


class CaulkFace {
	const HollowSettings& m_settings;
	const doublevector_vector_t& m_exclude_vec;
public:
	CaulkFace( const HollowSettings& settings, const doublevector_vector_t& exclude_vec ):
		m_settings( settings ), m_exclude_vec( exclude_vec ) {
	}
	void operator()( Face& face ) const {
		double dot = vector3_dot( face.getPlane().plane3().normal(), m_settings.m_exclusionAxis );
		if( dot == 0 || ( dot > m_settings.m_mindot + 0.001 && dot < m_settings.m_maxdot - 0.001 ) ) {
			for( doublevector_vector_t::const_iterator i = m_exclude_vec.begin(); i != m_exclude_vec.end(); ++i ) {
				if( ( *i ) == face.getPlane().plane3().normal() ) {
					return;
				}
			}
			face.SetShader( GetCaulkShader() );
		}
	}
};

class FaceMakeBrush {
	const Brush& m_brush;
	brush_vector_t& m_out;
	const HollowSettings& m_settings;
	doublevector_vector_t& exclude_vec;

public:
	FaceMakeBrush( const Brush& brush, brush_vector_t& out, const HollowSettings& settings, doublevector_vector_t& exclude_vec )
		: m_brush( brush ),
		  m_out( out ),
		  m_settings( settings ),
		  exclude_vec( exclude_vec ) {
	}
	void operator()( Face& face ) const {
		double dot = vector3_dot( face.getPlane().plane3().normal(), m_settings.m_exclusionAxis );
		if( dot == 0 || ( dot > m_settings.m_mindot + 0.001 && dot < m_settings.m_maxdot - 0.001 ) ) {
			for( doublevector_vector_t::const_iterator i = exclude_vec.begin(); i != exclude_vec.end(); ++i ) {
				if( ( *i ) == face.getPlane().plane3().normal() ) {
					return;
				}
			}

			if( m_settings.m_hollowType == ePull ) {
				if( face.contributes() ) {
					face.getPlane().offset( m_settings.m_offset );
					face.planeChanged();
					m_out.push_back( new Brush( m_brush ) );
					face.getPlane().offset( -m_settings.m_offset );
					face.planeChanged();

					if( m_settings.m_caulk ) {
						Brush_forEachFace( *m_out.back(), CaulkFace( m_settings, exclude_vec ) );
					}
					Face* newFace = m_out.back()->addFace( face );
					if( newFace != 0 ) {
						newFace->flipWinding();
					}
				}
			}
			else if( m_settings.m_hollowType == eWrap ) {
				//Face_makeBrush( face, brush, m_out, offset );
				if( face.contributes() ) {
					face.undoSave();
					m_out.push_back( new Brush( m_brush ) );
					if( !m_settings.m_removeInner && m_settings.m_caulk )
						face.SetShader( GetCaulkShader() );
					Face* newFace = m_out.back()->addFace( face );
					face.getPlane().offset( -m_settings.m_offset );
					face.planeChanged();
					if( m_settings.m_caulk )
						face.SetShader( GetCaulkShader() );
					if( newFace != 0 ) {
						newFace->flipWinding();
						newFace->getPlane().offset( m_settings.m_offset );
						newFace->planeChanged();
					}
				}
			}
			else if( m_settings.m_hollowType == eExtrude ) {
				if( face.contributes() ) {
					//face.undoSave();
					m_out.push_back( new Brush( m_brush ) );
					m_out.back()->clear();

					Face* newFace = m_out.back()->addFace( face );
					if( newFace != 0 ) {
						newFace->getPlane().offset( m_settings.m_offset );
						newFace->planeChanged();
					}

					if( !m_settings.m_removeInner && m_settings.m_caulk )
						face.SetShader( GetCaulkShader() );
					newFace = m_out.back()->addFace( face );
					if( newFace != 0 ) {
						newFace->flipWinding();
					}
					Winding& winding = face.getWinding();
					TextureProjection projection;
					TexDef_Construct_Default( projection );
					for( Winding::iterator j = winding.begin(); j != winding.end(); ++j ) {
						std::size_t index = std::distance( winding.begin(), j );
						std::size_t next = Winding_next( winding, index );

						m_out.back()->addPlane( winding[index].vertex,
												winding[next].vertex,
												winding[next].vertex + face.getPlane().plane3().normal() * m_settings.m_offset,
												TextureBrowser_GetSelectedShader( GlobalTextureBrowser() ),
												projection );
					}
				}
			}
			else if( m_settings.m_hollowType == eDiag ) {
				if( face.contributes() ) {
					m_out.push_back( new Brush( m_brush ) );
					m_out.back()->clear();

					Face* newFace = m_out.back()->addFace( face );
					if( newFace != 0 ) {
						newFace->planeChanged();
					}
					newFace = m_out.back()->addFace( face );

					if( newFace != 0 ) {
						if( !m_settings.m_removeInner && m_settings.m_caulk ){
							newFace->SetShader( GetCaulkShader() );
						}
						newFace->flipWinding();
						newFace->getPlane().offset( m_settings.m_offset );
						newFace->planeChanged();
					}

					Winding& winding = face.getWinding();
					TextureProjection projection;
					TexDef_Construct_Default( projection );
					for( Winding::iterator i = winding.begin(); i != winding.end(); ++i ) {
						std::size_t index = std::distance( winding.begin(), i );
						std::size_t next = Winding_next( winding, index );
						Vector3 BestPoint;
						float bestdist = 999999;

						Face* parallel_face = 0;
						for( Brush::const_iterator j = m_brush.begin(); j != m_brush.end(); ++j ) {
							if( vector3_equal_epsilon( face.getPlane().plane3().normal(), ( *j )->getPlane().plane3().normal(), 1e-6 ) ){
								parallel_face = ( *j );
								break;
							}
						}

						if( parallel_face ){
							Winding& winding2 = parallel_face->getWinding();
							float bestdot = -1;
							for( Winding::iterator k = winding2.begin(); k != winding2.end(); ++k ) {
								std::size_t index2 = std::distance( winding2.begin(), k );
								float dot = vector3_dot(
												vector3_normalised(
													vector3_cross(
														winding[index].vertex - winding[next].vertex,
														winding[index].vertex - winding2[index2].vertex
													)
												),
												face.getPlane().plane3().normal()
											);
								if( dot > bestdot ) {
									bestdot = dot;
									BestPoint = winding2[index2].vertex;
								}
							}
						}
						else{
							for( Brush::const_iterator j = m_brush.begin(); j != m_brush.end(); ++j ) {
								Winding& winding2 = ( *j )->getWinding();
								for( Winding::iterator k = winding2.begin(); k != winding2.end(); ++k ) {
									std::size_t index2 = std::distance( winding2.begin(), k );
									float testdist = vector3_length( winding[index].vertex - winding2[index2].vertex );
									if( testdist < bestdist ) {
										bestdist = testdist;
										BestPoint = winding2[index2].vertex;
									}
								}
							}
						}
						m_out.back()->addPlane( winding[next].vertex,
												winding[index].vertex,
												BestPoint,
												m_settings.m_caulk ? GetCaulkShader() : TextureBrowser_GetSelectedShader( GlobalTextureBrowser() ),
												projection );
					}
				}
			}
		}
	}
};

class FaceExclude {
	HollowSettings& m_settings;
public:
	FaceExclude( HollowSettings& settings )
		: m_settings( settings ) {
	}
	void operator()( Face& face ) const {
		double dot = vector3_dot( face.getPlane().plane3().normal(), m_settings.m_exclusionAxis );
		if( dot < m_settings.m_mindot ) {
			m_settings.m_mindot = dot;
		}
		else if( dot > m_settings.m_maxdot ) {
			m_settings.m_maxdot = dot;
		}
	}
};

class FaceOffset {
	const HollowSettings& m_settings;
	const doublevector_vector_t& m_exclude_vec;
public:
	FaceOffset( const HollowSettings& settings, const doublevector_vector_t& exclude_vec )
		: m_settings( settings ), m_exclude_vec( exclude_vec ) {
	}
	void operator()( Face& face ) const {
		const double dot = vector3_dot( face.getPlane().plane3().normal(), m_settings.m_exclusionAxis );
		if( dot == 0 || ( dot > m_settings.m_mindot + 0.001 && dot < m_settings.m_maxdot - 0.001 ) ) {
			for( doublevector_vector_t::const_iterator i = m_exclude_vec.begin(); i != m_exclude_vec.end(); ++i ) {
				if( ( *i ) == face.getPlane().plane3().normal() ) {
					return;
				}
			}
			face.undoSave();
			face.getPlane().offset( m_settings.m_offset );
			face.planeChanged();
		}
	}
};

class FaceExcludeSelected {
	doublevector_vector_t& m_outvec;
public:
	FaceExcludeSelected( doublevector_vector_t& outvec ): m_outvec( outvec ) {
	}
	void operator()( FaceInstance& face ) const {
		if( face.isSelected() ) {
			m_outvec.push_back( face.getFace().getPlane().plane3().normal() );
		}
	}
};


class BrushHollowSelectedWalker : public scene::Graph::Walker {
	HollowSettings& m_settings;

	float offset;
	EHollowType HollowType;
public:
	BrushHollowSelectedWalker( HollowSettings& settings )
		: m_settings( settings ) {
	}
	bool pre( const scene::Path& path, scene::Instance& instance ) const {
		if( path.top().get().visible() ) {
			Brush* brush = Node_getBrush( path.top() );
			if( brush != 0
					&& Instance_getSelectable( instance )->isSelected()
					&& path.size() > 1 ) {
				brush_vector_t out;
				doublevector_vector_t exclude_vec;
				m_settings.m_mindot = m_settings.m_maxdot = 0;

				if( m_settings.m_exclusionAxis != g_vector3_identity ) {
					Brush_forEachFace( *brush, FaceExclude( m_settings ) );
				}
				else {
					Brush_ForEachFaceInstance( *Instance_getBrush( instance ), FaceExcludeSelected( exclude_vec ) );
				}

				if( m_settings.m_hollowType == ePull ) {
					if( !m_settings.m_removeInner && m_settings.m_caulk ) {
						Brush_forEachFace( *brush, CaulkFace( m_settings, exclude_vec ) );
					}
					Brush* tmpbrush = new Brush( *brush );
					tmpbrush->removeEmptyFaces();
					Brush_forEachFace( *tmpbrush, FaceMakeBrush( *tmpbrush, out, m_settings, exclude_vec ) );
					delete tmpbrush;
				}
				else if( m_settings.m_hollowType == eDiag ) {
					Brush* tmpbrush = new Brush( *brush );
					Brush_forEachFace( *tmpbrush, FaceOffset( m_settings, exclude_vec ) );
					tmpbrush->removeEmptyFaces();
					Brush_forEachFace( *tmpbrush, FaceMakeBrush( *brush, out, m_settings, exclude_vec ) );
					delete tmpbrush;
					if( !m_settings.m_removeInner && m_settings.m_caulk ) {
						Brush_forEachFace( *brush, CaulkFace( m_settings, exclude_vec ) );
					}
				}
				else {
					Brush_forEachFace( *brush, FaceMakeBrush( *brush, out, m_settings, exclude_vec ) );
				}
				for( brush_vector_t::const_iterator i = out.begin(); i != out.end(); ++i ) {
					( *i )->removeEmptyFaces();
					if( ( *i )->hasContributingFaces() ) {
						NodeSmartReference node( ( new BrushNode() )->node() );
						Node_getBrush( node )->copy( *( *i ) );
						delete( *i );
						Node_getTraversable( path.parent() )->insert( node );
						//path.push( makeReference( node.get() ) );
						//selectPath( path, true );
						//Instance_getSelectable( *GlobalSceneGraph().find( path ) )->setSelected( true );
						//Path_deleteTop( path );
					}
				}
			}
		}
		return true;
	}
};

typedef std::list<Brush*> brushlist_t;

class BrushGatherSelected : public scene::Graph::Walker
{
brush_vector_t& m_brushlist;
public:
BrushGatherSelected( brush_vector_t& brushlist )
	: m_brushlist( brushlist ){
}
bool pre( const scene::Path& path, scene::Instance& instance ) const {
	if ( path.top().get().visible() ) {
		Brush* brush = Node_getBrush( path.top() );
		if ( brush != 0
			 && Instance_getSelectable( instance )->isSelected() ) {
			m_brushlist.push_back( brush );
		}
	}
	return true;
}
};
/*
class BrushDeleteSelected : public scene::Graph::Walker
{
public:
bool pre( const scene::Path& path, scene::Instance& instance ) const {
	return true;
}
void post( const scene::Path& path, scene::Instance& instance ) const {
	if ( path.top().get().visible() ) {
		Brush* brush = Node_getBrush( path.top() );
		if ( brush != 0
			 && Instance_getSelectable( instance )->isSelected()
			 && path.size() > 1 ) {
			Path_deleteTop( path );
		}
	}
}
};
*/
#include "ientity.h"

class BrushDeleteSelected : public scene::Graph::Walker
{
scene::Node* m_keepNode;
mutable bool m_eraseParent;
public:
BrushDeleteSelected( scene::Node* keepNode ): m_keepNode( keepNode ), m_eraseParent( false ){
}
BrushDeleteSelected(): m_keepNode( NULL ), m_eraseParent( false ){
}
bool pre( const scene::Path& path, scene::Instance& instance ) const {
	return true;
}
void post( const scene::Path& path, scene::Instance& instance ) const {
	//globalOutputStream() << path.size() << "\n";
	if ( path.top().get().visible() ) {
		Brush* brush = Node_getBrush( path.top() );
		if ( brush != 0
			 && Instance_getSelectable( instance )->isSelected()
			 && path.size() > 1 ) {
			scene::Node& parent = path.parent();
			Path_deleteTop( path );
			if( Node_getTraversable( parent )->empty() ){
				m_eraseParent = true;
				//globalOutputStream() << "Empty node?!.\n";
			}
		}
	}
	if( m_eraseParent && !Node_isPrimitive( path.top() ) && path.size() > 1 ){
		//globalOutputStream() << "about to Delete empty node!.\n";
		m_eraseParent = false;
		Entity* entity = Node_getEntity( path.top() );
		if ( entity != 0 && path.top().get_pointer() != Map_FindWorldspawn( g_map )
			&& Node_getTraversable( path.top() )->empty() && path.top().get_pointer() != m_keepNode ) {
			//globalOutputStream() << "now Deleting empty node!.\n";
			Path_deleteTop( path );
		}
	}
}
};


template<typename Type>
class RemoveReference
{
public:
typedef Type type;
};

template<typename Type>
class RemoveReference<Type&>
{
public:
typedef Type type;
};

template<typename Functor>
class Dereference
{
const Functor& functor;
public:
typedef typename RemoveReference<typename Functor::first_argument_type>::type* first_argument_type;
typedef typename Functor::result_type result_type;
Dereference( const Functor& functor ) : functor( functor ){
}
result_type operator()( first_argument_type firstArgument ) const {
	return functor( *firstArgument );
}
};

template<typename Functor>
inline Dereference<Functor> makeDereference( const Functor& functor ){
	return Dereference<Functor>( functor );
}

typedef Face* FacePointer;
const FacePointer c_nullFacePointer = 0;

template<typename Predicate>
Face* Brush_findIf( const Brush& brush, const Predicate& predicate ){
	Brush::const_iterator i = std::find_if( brush.begin(), brush.end(), makeDereference( predicate ) );
	return i == brush.end() ? c_nullFacePointer : *i; // uses c_nullFacePointer instead of 0 because otherwise gcc 4.1 attempts conversion to int
}

template<typename Caller>
class BindArguments1
{
typedef typename Caller::second_argument_type FirstBound;
FirstBound firstBound;
public:
typedef typename Caller::result_type result_type;
typedef typename Caller::first_argument_type first_argument_type;
BindArguments1( FirstBound firstBound )
	: firstBound( firstBound ){
}
result_type operator()( first_argument_type firstArgument ) const {
	return Caller::call( firstArgument, firstBound );
}
};

template<typename Caller>
class BindArguments2
{
typedef typename Caller::second_argument_type FirstBound;
typedef typename Caller::third_argument_type SecondBound;
FirstBound firstBound;
SecondBound secondBound;
public:
typedef typename Caller::result_type result_type;
typedef typename Caller::first_argument_type first_argument_type;
BindArguments2( FirstBound firstBound, SecondBound secondBound )
	: firstBound( firstBound ), secondBound( secondBound ){
}
result_type operator()( first_argument_type firstArgument ) const {
	return Caller::call( firstArgument, firstBound, secondBound );
}
};

template<typename Caller, typename FirstBound, typename SecondBound>
BindArguments2<Caller> bindArguments( const Caller& caller, FirstBound firstBound, SecondBound secondBound ){
	return BindArguments2<Caller>( firstBound, secondBound );
}

inline bool Face_testPlane( const Face& face, const Plane3& plane, bool flipped ){
	return face.contributes() && !Winding_TestPlane( face.getWinding(), plane, flipped );
}
typedef Function3<const Face&, const Plane3&, bool, bool, Face_testPlane> FaceTestPlane;



/// \brief Returns true if
/// \li !flipped && brush is BACK or ON
/// \li flipped && brush is FRONT or ON
bool Brush_testPlane( const Brush& brush, const Plane3& plane, bool flipped ){
	brush.evaluateBRep();
#if 1
	for ( Brush::const_iterator i( brush.begin() ); i != brush.end(); ++i )
	{
		if ( Face_testPlane( *( *i ), plane, flipped ) ) {
			return false;
		}
	}
	return true;
#else
	return Brush_findIf( brush, bindArguments( FaceTestPlane(), makeReference( plane ), flipped ) ) == 0;
#endif
}

brushsplit_t Brush_classifyPlane( const Brush& brush, const Plane3& plane ){
	brush.evaluateBRep();
	brushsplit_t split;
	for ( Brush::const_iterator i( brush.begin() ); i != brush.end(); ++i )
	{
		if ( ( *i )->contributes() ) {
			split += Winding_ClassifyPlane( ( *i )->getWinding(), plane );
		}
	}
	return split;
}

bool Brush_subtract( const Brush& brush, const Brush& other, brush_vector_t& ret_fragments ){
	if ( aabb_intersects_aabb( brush.localAABB(), other.localAABB() ) ) {
		brush_vector_t fragments;
		fragments.reserve( other.size() );
		Brush back( brush );

		for ( Brush::const_iterator i( other.begin() ); i != other.end(); ++i )
		{
			if ( ( *i )->contributes() ) {
				brushsplit_t split = Brush_classifyPlane( back, ( *i )->plane3() );
				if ( split.counts[ePlaneFront] != 0
					 && split.counts[ePlaneBack] != 0 ) {
					fragments.push_back( new Brush( back ) );
					Face* newFace = fragments.back()->addFace( *( *i ) );
					if ( newFace != 0 ) {
						newFace->flipWinding();
					}
					back.addFace( *( *i ) );
				}
				else if ( split.counts[ePlaneBack] == 0 ) {
					for ( brush_vector_t::iterator i = fragments.begin(); i != fragments.end(); ++i )
					{
						delete( *i );
					}
					return false;
				}
			}
		}
		ret_fragments.insert( ret_fragments.end(), fragments.begin(), fragments.end() );
		return true;
	}
	return false;
}

class SubtractBrushesFromUnselected : public scene::Graph::Walker
{
const brush_vector_t& m_brushlist;
std::size_t& m_before;
std::size_t& m_after;
mutable bool m_eraseParent;
public:
SubtractBrushesFromUnselected( const brush_vector_t& brushlist, std::size_t& before, std::size_t& after )
	: m_brushlist( brushlist ), m_before( before ), m_after( after ), m_eraseParent( false ){
}
bool pre( const scene::Path& path, scene::Instance& instance ) const {
	if ( path.top().get().visible() ) {
		return true;
	}
	return false;
}
void post( const scene::Path& path, scene::Instance& instance ) const {
	if ( path.top().get().visible() ) {
		Brush* brush = Node_getBrush( path.top() );
		if ( brush != 0
			 && !Instance_getSelectable( instance )->isSelected() ) {
			brush_vector_t buffer[2];
			bool swap = false;
			Brush* original = new Brush( *brush );
			buffer[static_cast<std::size_t>( swap )].push_back( original );

			{
				for ( brush_vector_t::const_iterator i( m_brushlist.begin() ); i != m_brushlist.end(); ++i )
				{
					for ( brush_vector_t::iterator j( buffer[static_cast<std::size_t>( swap )].begin() ); j != buffer[static_cast<std::size_t>( swap )].end(); ++j )
					{
						if ( Brush_subtract( *( *j ), *( *i ), buffer[static_cast<std::size_t>( !swap )] ) ) {
							delete ( *j );
						}
						else
						{
							buffer[static_cast<std::size_t>( !swap )].push_back( ( *j ) );
						}
					}
					buffer[static_cast<std::size_t>( swap )].clear();
					swap = !swap;
				}
			}

			brush_vector_t& out = buffer[static_cast<std::size_t>( swap )];

			if ( out.size() == 1 && out.back() == original ) {
				delete original;
			}
			else
			{
				++m_before;
				for ( brush_vector_t::const_iterator i = out.begin(); i != out.end(); ++i )
				{
					++m_after;
					( *i )->removeEmptyFaces();
					if ( !( *i )->empty() ) {
						NodeSmartReference node( ( new BrushNode() )->node() );
						Node_getBrush( node )->copy( *( *i ) );
						delete ( *i );
						Node_getTraversable( path.parent() )->insert( node );
					}
					else{
						delete ( *i );
					}
				}
				scene::Node& parent = path.parent();
				Path_deleteTop( path );
				if( Node_getTraversable( parent )->empty() ){
					m_eraseParent = true;
				}
			}
		}
	}
	if( m_eraseParent && !Node_isPrimitive( path.top() ) && path.size() > 1 ){
		m_eraseParent = false;
		Entity* entity = Node_getEntity( path.top() );
		if ( entity != 0 && path.top().get_pointer() != Map_FindWorldspawn( g_map )
			&& Node_getTraversable( path.top() )->empty() ) {
			Path_deleteTop( path );
		}
	}
}
};

void CSG_Subtract(){
	brush_vector_t selected_brushes;
	GlobalSceneGraph().traverse( BrushGatherSelected( selected_brushes ) );

	if ( selected_brushes.empty() ) {
		globalWarningStream() << "CSG Subtract: No brushes selected.\n";
	}
	else
	{
		globalOutputStream() << "CSG Subtract: Subtracting " << Unsigned( selected_brushes.size() ) << " brushes.\n";

		UndoableCommand undo( "brushSubtract" );

		// subtract selected from unselected
		std::size_t before = 0;
		std::size_t after = 0;
		GlobalSceneGraph().traverse( SubtractBrushesFromUnselected( selected_brushes, before, after ) );
		globalOutputStream() << "CSG Subtract: Result: "
							 << Unsigned( after ) << " fragment" << ( after == 1 ? "" : "s" )
							 << " from " << Unsigned( before ) << " brush" << ( before == 1 ? "" : "es" ) << ".\n";

		SceneChangeNotify();
	}
}

#include "clippertool.h"
class BrushSplitByPlaneSelected : public scene::Graph::Walker
{
const Plane3 m_plane;
const ClipperPoints m_points;
const char* m_shader;
const TextureProjection& m_projection;
const bool m_split; /* split or clip */
public:
BrushSplitByPlaneSelected( const ClipperPoints& points, const char* shader, const TextureProjection& projection, bool split )
	: m_plane( plane3_for_points( points._points ) ), m_points( points ), m_shader( shader ), m_projection( projection ), m_split( split ){
}
bool pre( const scene::Path& path, scene::Instance& instance ) const {
	return true;
}
void post( const scene::Path& path, scene::Instance& instance ) const {
	if ( path.top().get().visible() ) {
		Brush* brush = Node_getBrush( path.top() );
		if ( brush != 0
			 && Instance_getSelectable( instance )->isSelected() ) {
			const brushsplit_t split = Brush_classifyPlane( *brush, m_plane );
			if ( split.counts[ePlaneBack] && split.counts[ePlaneFront] ) {
				// the plane intersects this brush
				if ( m_split ) {
					NodeSmartReference node( ( new BrushNode() )->node() );
					Brush* fragment = Node_getBrush( node );
					fragment->copy( *brush );
					fragment->addPlane( m_points[0], m_points[2], m_points[1], m_shader, m_projection ); /* flip plane points */
					fragment->removeEmptyFaces();
					ASSERT_MESSAGE( !fragment->empty(), "brush left with no faces after split" );

					Node_getTraversable( path.parent() )->insert( node );
					{
						scene::Path fragmentPath = path;
						fragmentPath.top() = makeReference( node.get() );
						selectPath( fragmentPath, true );
					}
				}

				brush->addPlane( m_points[0], m_points[1], m_points[2], m_shader, m_projection );
				brush->removeEmptyFaces();
				ASSERT_MESSAGE( !brush->empty(), "brush left with no faces after split" );
			}
			else
			// the plane does not intersect this brush
			if ( !m_split && split.counts[ePlaneFront] != 0 ) {
				// the brush is "behind" the plane
				Path_deleteTop( path );
			}
		}
	}
}
};

void Scene_BrushSplitByPlane( scene::Graph& graph, const ClipperPoints& points, bool caulk, bool split ){
	const char* shader = caulk? GetCaulkShader() : TextureBrowser_GetSelectedShader( GlobalTextureBrowser() );
	TextureProjection projection;
	TexDef_Construct_Default( projection );
	graph.traverse( BrushSplitByPlaneSelected( points, shader, projection, split ) );
	SceneChangeNotify();
}


class BrushInstanceSetClipPlane : public scene::Graph::Walker
{
const Plane3 m_plane;
public:
BrushInstanceSetClipPlane( const ClipperPoints& points )
	: m_plane( plane3_for_points( points._points ) ){
}
bool pre( const scene::Path& path, scene::Instance& instance ) const {
	BrushInstance* brush = Instance_getBrush( instance );
	if ( brush != 0
		 && path.top().get().visible()
		 && brush->isSelected() ) {
		BrushInstance& brushInstance = *brush;
		brushInstance.setClipPlane( m_plane );
	}
	return true;
}
};

void Scene_BrushSetClipPlane( scene::Graph& graph, const ClipperPoints& points ){
	graph.traverse( BrushInstanceSetClipPlane( points ) );
}

/*
   =============
   CSG_Merge
   =============
 */
bool Brush_merge( Brush& brush, const brush_vector_t& in, bool onlyshape ){
	// gather potential outer faces

	{
		typedef std::vector<const Face*> Faces;
		Faces faces;
		for ( brush_vector_t::const_iterator i( in.begin() ); i != in.end(); ++i )
		{
			( *i )->evaluateBRep();
			for ( Brush::const_iterator j( ( *i )->begin() ); j != ( *i )->end(); ++j )
			{
				if ( !( *j )->contributes() ) {
					continue;
				}

				const Face& face1 = *( *j );

				bool skip = false;
				// test faces of all input brushes
				//!\todo SPEEDUP: Flag already-skip faces and only test brushes from i+1 upwards.
				for ( brush_vector_t::const_iterator k( in.begin() ); !skip && k != in.end(); ++k )
				{
					if ( k != i ) { // don't test a brush against itself
						for ( Brush::const_iterator l( ( *k )->begin() ); !skip && l != ( *k )->end(); ++l )
						{
							const Face& face2 = *( *l );

							// face opposes another face
							if ( plane3_opposing( face1.plane3(), face2.plane3() ) ) {
								// skip opposing planes
								skip  = true;
								break;
							}
						}
					}
				}

				// check faces already stored
				for ( Faces::const_iterator m = faces.begin(); !skip && m != faces.end(); ++m )
				{
					const Face& face2 = *( *m );

					// face equals another face
					if ( plane3_equal( face1.plane3(), face2.plane3() ) ) {
						//if the texture/shader references should be the same but are not
						if ( !onlyshape && !shader_equal( face1.getShader().getShader(), face2.getShader().getShader() ) ) {
							return false;
						}
						// skip duplicate planes
						skip = true;
						break;
					}

					// face1 plane intersects face2 winding or vice versa
					if ( Winding_PlanesConcave( face1.getWinding(), face2.getWinding(), face1.plane3(), face2.plane3() ) ) {
						// result would not be convex
						return false;
					}
				}

				if ( !skip ) {
					faces.push_back( &face1 );
				}
			}
		}
		for ( Faces::const_iterator i = faces.begin(); i != faces.end(); ++i )
		{
			if ( !brush.addFace( *( *i ) ) ) {
				// result would have too many sides
				return false;
			}
		}
	}

	brush.removeEmptyFaces();

	return true;
}

void CSG_Merge( void ){
	brush_vector_t selected_brushes;

	// remove selected
	GlobalSceneGraph().traverse( BrushGatherSelected( selected_brushes ) );

	if ( selected_brushes.empty() ) {
		globalWarningStream() << "CSG Merge: No brushes selected.\n";
		return;
	}

	if ( selected_brushes.size() < 2 ) {
		globalWarningStream() << "CSG Merge: At least two brushes have to be selected.\n";
		return;
	}

	globalOutputStream() << "CSG Merge: Merging " << Unsigned( selected_brushes.size() ) << " brushes.\n";

	UndoableCommand undo( "brushMerge" );

	scene::Path merged_path = GlobalSelectionSystem().ultimateSelected().path();

	NodeSmartReference node( ( new BrushNode() )->node() );
	Brush* brush = Node_getBrush( node );
	// if the new brush would not be convex
	if ( !Brush_merge( *brush, selected_brushes, true ) ) {
		globalWarningStream() << "CSG Merge: Failed - result would not be convex.\n";
	}
	else
	{
		ASSERT_MESSAGE( !brush->empty(), "brush left with no faces after merge" );

		// free the original brushes
		GlobalSceneGraph().traverse( BrushDeleteSelected( merged_path.parent().get_pointer() ) );

		merged_path.pop();
		Node_getTraversable( merged_path.top() )->insert( node );
		merged_path.push( makeReference( node.get() ) );

		selectPath( merged_path, true );

		globalOutputStream() << "CSG Merge: Succeeded.\n";
		SceneChangeNotify();
	}
}






/*
   =============
   CSG_Tool
   =============
 */
#include "mainframe.h"
#include <gtk/gtk.h>
#include "gtkutil/dialog.h"
#include "gtkutil/button.h"
#include "gtkutil/accelerator.h"
#include "xywindow.h"
#include "camwindow.h"

struct CSGToolDialog
{
	GtkSpinButton* spin;
	GtkWindow *window;
	GtkToggleButton *radFaces, *radProj, *radCam, *caulk, *removeInner;
};

CSGToolDialog g_csgtool_dialog;

#if 0
DoubleVector3 getExclusion(){
	if( gtk_toggle_button_get_active( g_csgtool_dialog.radProj ) ){
		if( GlobalXYWnd_getCurrentViewType() == YZ ){
			return DoubleVector3( 1, 0, 0 );
		}
		else if( GlobalXYWnd_getCurrentViewType() == XZ ){
			return DoubleVector3( 0, 1, 0 );
		}
		else if( GlobalXYWnd_getCurrentViewType() == XY ){
			return DoubleVector3( 0, 0, 1 );
		}
	}
	if( gtk_toggle_button_get_active( g_csgtool_dialog.radCam ) ){
		Vector3 angles( Camera_getAngles( *g_pParentWnd->GetCamWnd() ) );
//		globalOutputStream() << angles << " angles\n";
		DoubleVector3 radangles( degrees_to_radians( angles[0] ), degrees_to_radians( angles[1] ), degrees_to_radians( angles[2] ) );
//		globalOutputStream() << radangles << " radangles\n";
//		x = cos(yaw)*cos(pitch)
//		y = sin(yaw)*cos(pitch)
//		z = sin(pitch)
		DoubleVector3 viewvector;
		viewvector[0] = cos( radangles[1] ) * cos( radangles[0] );
		viewvector[1] = sin( radangles[1] ) * cos( radangles[0] );
		viewvector[2] = sin( radangles[0] );
//		globalOutputStream() << viewvector << " viewvector\n";
		return viewvector;
	}
	return DoubleVector3( 0, 0, 0 );
}
#endif

class BrushFaceOffset {
	HollowSettings& m_settings;
public:
	BrushFaceOffset( HollowSettings& settings )
		: m_settings( settings ) {
	}
	void operator()( BrushInstance& brush ) const {
		m_settings.m_mindot = m_settings.m_maxdot = 0;
		doublevector_vector_t exclude_vec;
		if( m_settings.m_exclusionAxis != g_vector3_identity ) {
			Brush_forEachFace( brush, FaceExclude( m_settings ) );
		}
		else {
			Brush_ForEachFaceInstance( brush, FaceExcludeSelected( exclude_vec ) );
		}
		Brush_forEachFace( brush, FaceOffset( m_settings, exclude_vec ) );
	}
};

/*
   =============
   CSG_MakeRoom
   =============
 */
void CSG_MakeRoom(){
	HollowSettings settings;
	settings.m_offset = GetGridSize();
	settings.m_exclusionAxis = g_vector3_identity;
	settings.m_caulk = true;
	settings.m_removeInner = true;
	settings.m_hollowType = ePull;

	UndoableCommand undo( "makeRoom" );
	GlobalSceneGraph().traverse( BrushHollowSelectedWalker( settings ) );
	GlobalSceneGraph().traverse( BrushDeleteSelected() );
	SceneChangeNotify();
}

void CSGdlg_getSettings( HollowSettings& settings, const CSGToolDialog& dialog ){
	gtk_spin_button_update( dialog.spin );
	settings.m_offset = static_cast<float>( gtk_spin_button_get_value( dialog.spin ) );
	settings.m_exclusionAxis = g_vector3_identity;
	if( gtk_toggle_button_get_active( dialog.radProj ) ){
		settings.m_exclusionAxis[ static_cast<int>( GlobalXYWnd_getCurrentViewType() ) ] = 1;
	}
	else if( gtk_toggle_button_get_active( dialog.radCam ) ){
		settings.m_exclusionAxis = Camera_getViewVector( *g_pParentWnd->GetCamWnd() );
	}
	settings.m_caulk = gtk_toggle_button_get_active( dialog.caulk );
	settings.m_removeInner = gtk_toggle_button_get_active( dialog.removeInner );
}

void CSG_Hollow( EHollowType type, const char* undoString, const CSGToolDialog& dialog ){
	HollowSettings settings;
	CSGdlg_getSettings( settings, dialog );
	settings.m_hollowType = type;
	UndoableCommand undo( undoString );
	GlobalSceneGraph().traverse( BrushHollowSelectedWalker( settings ) );
	if( settings.m_removeInner ){
		GlobalSceneGraph().traverse( BrushDeleteSelected() );
	}
	SceneChangeNotify();
}

//=================DLG

static gboolean CSGdlg_HollowDiag( GtkWidget *widget, CSGToolDialog* dialog ){
	CSG_Hollow( eDiag, "brushHollow::Diag", *dialog );
	return TRUE;
}

static gboolean CSGdlg_HollowWrap( GtkWidget *widget, CSGToolDialog* dialog ){
	CSG_Hollow( eWrap, "brushHollow::Wrap", *dialog );
	return TRUE;
}

static gboolean CSGdlg_HollowExtrude( GtkWidget *widget, CSGToolDialog* dialog ){
	CSG_Hollow( eExtrude, "brushHollow::Extrude", *dialog );
	return TRUE;
}

static gboolean CSGdlg_HollowPull( GtkWidget *widget, CSGToolDialog* dialog ){
	CSG_Hollow( ePull, "brushHollow::Pull", *dialog );
	return TRUE;
}

static gboolean CSGdlg_BrushShrink( GtkWidget *widget, CSGToolDialog* dialog ){
	HollowSettings settings;
	CSGdlg_getSettings( settings, *dialog );
	settings.m_offset *= -1;
	UndoableCommand undo( "Shrink brush" );
//	GlobalSceneGraph().traverse( OffsetBrushFacesSelectedWalker( offset ) );
	//Scene_ForEachSelectedBrush_ForEachFace( GlobalSceneGraph(), BrushFaceOffset( offset ) );
	Scene_forEachSelectedBrush( BrushFaceOffset( settings ) );
	SceneChangeNotify();
	return TRUE;
}

static gboolean CSGdlg_BrushExpand( GtkWidget *widget, CSGToolDialog* dialog ){
	HollowSettings settings;
	CSGdlg_getSettings( settings, *dialog );
	UndoableCommand undo( "Expand brush" );
//	GlobalSceneGraph().traverse( OffsetBrushFacesSelectedWalker( offset ) );
	//Scene_ForEachSelectedBrush_ForEachFace( GlobalSceneGraph(), BrushFaceOffset( offset ) );
	Scene_forEachSelectedBrush( BrushFaceOffset( settings ) );
	SceneChangeNotify();
	return TRUE;
}

static gboolean CSGdlg_grid2spin( GtkWidget *widget, CSGToolDialog* dialog ){
	gtk_spin_button_set_value( dialog->spin, GetGridSize() );
	return TRUE;
}

static gboolean CSGdlg_delete( GtkWidget *widget, GdkEventAny *event, CSGToolDialog* dialog ){
	gtk_widget_hide( GTK_WIDGET( dialog->window ) );
	return TRUE;
}

void CSG_Tool(){
	if ( g_csgtool_dialog.window == NULL ) {
		g_csgtool_dialog.window = create_dialog_window( MainFrame_getWindow(), "CSG Tool", G_CALLBACK( CSGdlg_delete ), &g_csgtool_dialog );
		gtk_window_set_type_hint( g_csgtool_dialog.window, GDK_WINDOW_TYPE_HINT_UTILITY );

		//GtkAccelGroup* accel = gtk_accel_group_new();
		//gtk_window_add_accel_group( g_csgtool_dialog.window, accel );
		global_accel_connect_window( g_csgtool_dialog.window );

		{
			GtkHBox* hbox = create_dialog_hbox( 4, 4 );
			gtk_container_add( GTK_CONTAINER( g_csgtool_dialog.window ), GTK_WIDGET( hbox ) );
			{
				GtkTable* table = create_dialog_table( 3, 8, 4, 4 );
				gtk_box_pack_start( GTK_BOX( hbox ), GTK_WIDGET( table ), TRUE, TRUE, 0 );
				{
					//GtkWidget* label = gtk_label_new( "<->" );
					//gtk_widget_show( label );
					GtkWidget* button = gtk_button_new_with_label( "Grid->" );
					gtk_table_attach( table, button, 0, 1, 0, 1,
									  (GtkAttachOptions) ( 0 ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_show( button );
					g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( CSGdlg_grid2spin ), &g_csgtool_dialog );
				}
				{
					GtkAdjustment* adj = GTK_ADJUSTMENT( gtk_adjustment_new( 16, 0, 9999, 1, 10, 0 ) );
					GtkSpinButton* spin = GTK_SPIN_BUTTON( gtk_spin_button_new( adj, 1, 3 ) );
					gtk_widget_show( GTK_WIDGET( spin ) );
					gtk_widget_set_tooltip_text( GTK_WIDGET( spin ), "Thickness" );
					gtk_table_attach( table, GTK_WIDGET( spin ), 1, 2, 0, 1,
									  (GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_size_request( GTK_WIDGET( spin ), 64, -1 );
					gtk_spin_button_set_numeric( spin, TRUE );

					g_csgtool_dialog.spin = spin;
				}
				{
					//radio button group for choosing the exclude axis
					GtkWidget* radFaces = gtk_radio_button_new_with_label( NULL, "-faces" );
					gtk_widget_set_tooltip_text( radFaces, "Exclude selected faces" );
					GtkWidget* radProj = gtk_radio_button_new_with_label_from_widget( GTK_RADIO_BUTTON(radFaces), "-proj" );
					gtk_widget_set_tooltip_text( radProj, "Exclude faces, most orthogonal to active projection" );
					GtkWidget* radCam = gtk_radio_button_new_with_label_from_widget( GTK_RADIO_BUTTON(radFaces), "-cam" );
					gtk_widget_set_tooltip_text( radCam, "Exclude faces, most orthogonal to camera view" );

					gtk_widget_show( radFaces );
					gtk_widget_show( radProj );
					gtk_widget_show( radCam );

					gtk_table_attach( table, radFaces, 2, 3, 0, 1,
									(GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
									(GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_table_attach( table, radProj, 3, 4, 0, 1,
									(GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
									(GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_table_attach( table, radCam, 4, 5, 0, 1,
									(GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
									(GtkAttachOptions) ( 0 ), 0, 0 );

					g_csgtool_dialog.radFaces = GTK_TOGGLE_BUTTON( radFaces );
					g_csgtool_dialog.radProj = GTK_TOGGLE_BUTTON( radProj );
					g_csgtool_dialog.radCam = GTK_TOGGLE_BUTTON( radCam );
				}
				{
					GtkWidget* button = gtk_toggle_button_new();
					button_set_icon( GTK_BUTTON( button ), "f-caulk.png" );
					gtk_button_set_relief( GTK_BUTTON( button ), GTK_RELIEF_NONE );
					gtk_table_attach( table, button, 6, 7, 0, 1,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Caulk some faces" );
					gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( button ), TRUE );
					gtk_widget_show( button );
					g_csgtool_dialog.caulk = GTK_TOGGLE_BUTTON( button );
				}
				{
					GtkWidget* button = gtk_toggle_button_new();
					button_set_icon( GTK_BUTTON( button ), "csgtool_removeinner.png" );
					gtk_button_set_relief( GTK_BUTTON( button ), GTK_RELIEF_NONE );
					gtk_table_attach( table, button, 7, 8, 0, 1,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Remove inner brush" );
					gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( button ), TRUE );
					gtk_widget_show( button );
					g_csgtool_dialog.removeInner = GTK_TOGGLE_BUTTON( button );
				}
				{
					GtkWidget* sep = gtk_hseparator_new();
					gtk_widget_show( sep );
					gtk_table_attach( table, sep, 0, 8, 1, 2,
									(GtkAttachOptions) ( GTK_EXPAND | GTK_FILL ),
									(GtkAttachOptions) ( 0 ), 0, 0 );
				}
				{
					GtkWidget* button = gtk_button_new();
					button_set_icon( GTK_BUTTON( button ), "csgtool_shrink.png" );
					gtk_table_attach( table, button, 0, 1, 2, 3,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Shrink brush" );
					gtk_widget_show( button );
					g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( CSGdlg_BrushShrink ), &g_csgtool_dialog );
				}
				{
					GtkWidget* button = gtk_button_new();
					button_set_icon( GTK_BUTTON( button ), "csgtool_expand.png" );
					gtk_table_attach( table, button, 1, 2, 2, 3,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Expand brush" );
					gtk_widget_show( button );
					g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( CSGdlg_BrushExpand ), &g_csgtool_dialog );
				}
				{
					GtkWidget* button = gtk_button_new();
					button_set_icon( GTK_BUTTON( button ), "csgtool_diagonal.png" );
					gtk_table_attach( table, button, 3, 4, 2, 3,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Hollow::diagonal joints" );
					gtk_widget_show( button );
					g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( CSGdlg_HollowDiag ), &g_csgtool_dialog );
				}
				{
					GtkWidget* button = gtk_button_new();
					button_set_icon( GTK_BUTTON( button ), "csgtool_wrap.png" );
					gtk_table_attach( table, button, 4, 5, 2, 3,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Hollow::wrap" );
					gtk_widget_show( button );
					g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( CSGdlg_HollowWrap ), &g_csgtool_dialog );
				}
				{
					GtkWidget* button = gtk_button_new();
					button_set_icon( GTK_BUTTON( button ), "csgtool_extrude.png" );
					gtk_table_attach( table, button, 5, 6, 2, 3,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Hollow::extrude faces" );
					gtk_widget_show( button );
					g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( CSGdlg_HollowExtrude ), &g_csgtool_dialog );
				}
				{
					GtkWidget* button = gtk_button_new();
					button_set_icon( GTK_BUTTON( button ), "csgtool_pull.png" );
					gtk_table_attach( table, button, 6, 7, 2, 3,
									  (GtkAttachOptions) ( GTK_EXPAND ),
									  (GtkAttachOptions) ( 0 ), 0, 0 );
					gtk_widget_set_tooltip_text( button, "Hollow::pull faces" );
					gtk_widget_show( button );
					g_signal_connect( G_OBJECT( button ), "clicked", G_CALLBACK( CSGdlg_HollowPull ), &g_csgtool_dialog );
				}

			}
		}
	}

	gtk_widget_show( GTK_WIDGET( g_csgtool_dialog.window ) );
	gtk_window_present( g_csgtool_dialog.window );
}

