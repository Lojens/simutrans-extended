/*
 * Copyright (c) 2008 by Markus Pristovsek
 *
 * This file is part of the Simutrans project under the artistic license.
 */

#include <stdio.h>
#include <math.h>

#include "../simdebug.h"
#include "../simworld.h"
#include "../simobj.h"
#include "../display/simimg.h"
#include "../player/simplay.h"
#include "../utils/simrandom.h"
#include "../simtypes.h"
#include "../simunits.h"

#include "../boden/grund.h"

#include "../besch/groundobj_besch.h"

#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"

#include "../dataobj/loadsave.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"

#include "../obj/baum.h"

#include "movingobj.h"

/******************************** static stuff: besch management ****************************************************************/

vector_tpl<const groundobj_besch_t *> movingobj_t::movingobj_typen(0);

stringhashtable_tpl<groundobj_besch_t *> movingobj_t::besch_names;


bool compare_groundobj_besch(const groundobj_besch_t* a, const groundobj_besch_t* b);


bool movingobj_t::alles_geladen()
{
	movingobj_typen.resize(besch_names.get_count());
	FOR(stringhashtable_tpl<groundobj_besch_t*>, const& i, besch_names) {
		movingobj_typen.insert_ordered(i.value, compare_groundobj_besch);
	}
	// iterate again to assign the index
	FOR(stringhashtable_tpl<groundobj_besch_t*>, const& i, besch_names) {
		i.value->index = movingobj_typen.index_of(i.value);
	}

	if(besch_names.empty()) {
		movingobj_typen.append( NULL );
		DBG_MESSAGE("movingobj_t", "No movingobj found - feature disabled");
	}
	return true;
}



bool movingobj_t::register_besch(groundobj_besch_t *besch)
{
	// remove duplicates
	if(  besch_names.remove( besch->get_name() )  ) {
		dbg->warning( "movingobj_t::register_besch()", "Object %s was overlaid by addon!", besch->get_name() );
	}
	besch_names.put(besch->get_name(), besch );
	return true;
}




/* also checks for distribution values
 * @author prissi
 */
const groundobj_besch_t *movingobj_t::random_movingobj_for_climate(climate cl)
{
	// none there
	if(  besch_names.empty()  ) {
		return NULL;
	}

	int weight = 0;

	FOR(vector_tpl<groundobj_besch_t const*>, const i, movingobj_typen) {
		if (i->is_allowed_climate(cl) ) {
			weight += i->get_distribution_weight();
		}
	}

	// now weight their distribution
	if (weight > 0) {
		const int w=simrand(weight, "const groundobj_besch_t *movingobj_t::random_movingobj_for_climate");
		weight = 0;
		FOR(vector_tpl<groundobj_besch_t const*>, const i, movingobj_typen) {
			if (i->is_allowed_climate(cl)) {
				weight += i->get_distribution_weight();
				if(weight>=w) {
					return i;
				}
			}
		}
	}
	return NULL;
}



/******************************* end of static ******************************************/



// recalculates only the seasonal image
void movingobj_t::calc_bild()
{
	// alter/2048 is the age of the tree
	const groundobj_besch_t *besch=get_besch();
	const uint8 seasons = besch->get_seasons()-1;
	uint8 season = 0;

	switch(  seasons  ) {
		case 0: { // summer only
			season = 0;
			break;
		}
		case 1: { // summer, snow
			season = welt->get_snowline() <= get_pos().z  ||  welt->get_climate( get_pos().get_2d() ) == arctic_climate;
			break;
		}
		case 2: { // summer, winter, snow
			season = welt->get_snowline() <= get_pos().z  ||  welt->get_climate( get_pos().get_2d() ) == arctic_climate ? 2 : welt->get_season() == 1;
			break;
		}
		default: {
			if(  welt->get_snowline() <= get_pos().z  ||  welt->get_climate( get_pos().get_2d() ) == arctic_climate  ) {
				season = seasons;
			}
			else {
				// resolution 1/8th month (0..95)
				season = (seasons * (welt->get_yearsteps() + 1) - 1) / 96;
			}
			break;
		}
	}
	set_bild( get_besch()->get_bild( season, ribi_t::get_dir(get_direction()) )->get_nummer() );
}


movingobj_t::movingobj_t(loadsave_t *file) : 
#ifdef INLINE_OBJ_TYPE
    vehicle_base_t(movingobj)
#else
    vehicle_base_t()
#endif
{
	rdwr(file);
	if(get_besch()) {
		welt->sync_add( this );
	}
}


movingobj_t::movingobj_t(koord3d pos, const groundobj_besch_t *b ) : 
#ifdef INLINE_OBJ_TYPE
    vehicle_base_t(movingobj, pos)
#else
    vehicle_base_t(pos)
#endif
{
	movingobjtype = movingobj_typen.index_of(b);
	weg_next = 0;
	timetochange = 0;	// will do random direct change anyway during next step
	direction = calc_set_richtung( koord3d(0,0,0), koord3d(koord::west,0) );
	calc_bild();
	welt->sync_add( this );
}


movingobj_t::~movingobj_t()
{
	welt->sync_remove( this );
}


bool movingobj_t::check_season(const bool)
{
	const image_id old_image = get_bild();
	calc_bild();

	if(  get_bild() != old_image  ) {
		mark_image_dirty( get_bild(), 0 );
	}
	return true;
}


void movingobj_t::rdwr(loadsave_t *file)
{
	xml_tag_t d( file, "movingobj_t" );

	vehicle_base_t::rdwr(file);

	file->rdwr_enum(direction);
	if (file->is_loading()) {
		// restore dxdy information
		dx = dxdy[ ribi_t::get_dir(direction)*2];
		dy = dxdy[ ribi_t::get_dir(direction)*2+1];
	}

	file->rdwr_byte(steps);
	file->rdwr_byte(steps_next);

	pos_next.rdwr(file);
	koord p = pos_next_next.get_2d();
	p.rdwr(file);
	if(file->is_loading()) {
		pos_next_next = koord3d(p, 0);
		// z-coordinate will be restored in hop_check
	}

	file->rdwr_short(timetochange);

	if(file->is_saving()) {
		const char *s = get_besch()->get_name();
		file->rdwr_str(s);
	}
	else {
		char bname[128];
		file->rdwr_str(bname, lengthof(bname));
		groundobj_besch_t *besch = besch_names.get(bname);
		if(  besch_names.empty()  ||  besch==NULL  ) {
			movingobjtype = simrand(movingobj_typen.get_count(), "void movingobj_t::rdwr");
		}
		else {
			movingobjtype = (uint8)besch->get_index();
		}
		// if not there, besch will be zero

		use_calc_height = true;
	}
	weg_next = 0;
}



/**
 * Open a new observation window for the object.
 * @author Hj. Malthaner
 */
void movingobj_t::zeige_info()
{
	if(env_t::tree_info) {
		obj_t::zeige_info();
	}
}



/**
 * @return Einen Beschreibungsstring f�r das Objekt, der z.B. in einem
 * Beobachtungsfenster angezeigt wird.
 * @author Hj. Malthaner
 */
void movingobj_t::info(cbuffer_t & buf, bool dummy) const
{
	obj_t::info(buf);

	buf.append(translator::translate(get_besch()->get_name()));
	if (char const* const maker = get_besch()->get_copyright()) {
		buf.append("\n");
		buf.printf(translator::translate("Constructed by %s"), maker);
	}
	buf.append("\n");
	buf.append(translator::translate("cost for removal"));
	char buffer[128];
	money_to_string( buffer, get_besch()->get_preis()/100.0 );
	buf.append( buffer );
}



void movingobj_t::entferne(player_t *player)
{
	player_t::book_construction_costs(player, -get_besch()->get_preis(), get_pos().get_2d(), ignore_wt);
	mark_image_dirty( get_bild(), 0 );
	welt->sync_remove( this );
}




bool movingobj_t::sync_step(long delta_t)
{
	weg_next += get_besch()->get_speed() * delta_t;
	weg_next -= fahre_basis( weg_next );
	return true;
}



/* essential to find out about next step
 * returns true, if we can go here
 * (identical to fahrer)
 */
bool movingobj_t::check_next_tile( const grund_t *gr ) const
{
	if(gr==NULL) {
		// no ground => we cannot check further
		return false;
	}

	const groundobj_besch_t *besch = get_besch();
	if( !besch->is_allowed_climate( welt->get_climate(gr->get_pos().get_2d()) ) ) {
		// not an allowed climate zone!
		return false;
	}

	if(besch->get_waytype()==road_wt) {
		// can cross roads
		if(gr->get_typ()!=grund_t::boden  ||  !hang_t::ist_wegbar(gr->get_grund_hang())) {
			return false;
		}
		// only on roads, do not walk in cities
		if(gr->hat_wege()  &&  (!gr->hat_weg(road_wt)  ||  gr->get_weg(road_wt)->hat_gehweg())) {
			return false;
		}
		if(!besch->can_built_trees_here()) {
			return gr->find<baum_t>()==NULL;
		}
	}
	else if(besch->get_waytype()==air_wt) {
		// avoid towns to avoid flying through houses
		return gr->get_typ()==grund_t::boden  ||  gr->get_typ()==grund_t::wasser;
	}
	else if(besch->get_waytype()==water_wt) {
		// floating object
		return gr->get_typ()==grund_t::wasser  ||  gr->hat_weg(water_wt);
	}
	else if(besch->get_waytype()==ignore_wt) {
		// crosses nothing
		if(!gr->ist_natur()  ||  !hang_t::ist_wegbar(gr->get_grund_hang())) {
			return false;
		}
		if(!besch->can_built_trees_here()) {
			return gr->find<baum_t>()==NULL;
		}
	}
	return true;
}



grund_t* movingobj_t::hop_check()
{
	/* since we may be going diagonal without any road
	 * determining the next koord is a little tricky:
	 * If it is a diagonal, pos_next_next is calculated from current pos,
	 * Else pos_next_next is a single step from pos_next.
	 * otherwise objects would jump left/right on some diagonals
	 */
	koord k(direction);
	if (timetochange != 0) {
		koord k(direction);
		if(k.x&k.y) {
			pos_next_next = get_pos() + k;
		}
		else {
			pos_next_next = pos_next + k;
		}

		grund_t *gr = welt->lookup_kartenboden(pos_next_next.get_2d());
		if (check_next_tile(gr)) {
			pos_next_next = gr->get_pos();
		}
		else {
			timetochange = 0;
		}
	}

	if(timetochange==0) {
		// direction change needed
		timetochange = simrand(speed_to_kmh(get_besch()->get_speed())/3, "bool movingobj_t::hop_check()");
		const koord pos=pos_next.get_2d();
		const grund_t *to[4];
		uint8 until=0;
		// find all tiles we can go
		for(  int i=0;  i<4;  i++  ) {
			const grund_t *check = welt->lookup_kartenboden(pos+koord::nsow[i]);
			if(check_next_tile(check)  &&  check->get_pos()!=get_pos()) {
				to[until++] = check;
			}
		}
		// if nothing found, return
		if(until==0) {
			pos_next_next = get_pos();
			// (better would be destruction?)
		}
		else {
			// else prepare for direction change
			const grund_t *next = to[simrand(until, "bool movingobj_t::hop_check()")];
			pos_next_next = next->get_pos();
		}
	}
	else {
		timetochange--;
	}

	return welt->lookup(pos_next);
}



void movingobj_t::hop(grund_t* gr)
{
	verlasse_feld();

	if(pos_next.get_2d()==get_pos().get_2d()) {
		direction = ribi_t::rueckwaerts(direction);
		dx = -dx;
		dy = -dy;
		calc_bild();
	}
	else {
		ribi_t::ribi old_dir = direction;
		direction = calc_set_richtung( get_pos(), pos_next_next );
		if(old_dir!=direction) {
			calc_bild();
		}
	}

	set_pos(pos_next);
	betrete_feld(gr);
	// next position
	pos_next = pos_next_next;
}



void *movingobj_t::operator new(size_t /*s*/)
{
	return freelist_t::gimme_node(sizeof(movingobj_t));
}



void movingobj_t::operator delete(void *p)
{
	freelist_t::putback_node(sizeof(movingobj_t),p);
}
