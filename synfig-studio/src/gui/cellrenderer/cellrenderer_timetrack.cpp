/* === S Y N F I G ========================================================= */
/*!	\file cellrenderer_timetrack.cpp
**	\brief Cell renderer for the timetrack. Render all time points (waypoints / keyframes and current time line ...)
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**	......... ... 2018 Ivan Mahonin
**
**	This file is part of Synfig.
**
**	Synfig is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 2 of the License, or
**	(at your option) any later version.
**
**	Synfig is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with Synfig.  If not, see <https://www.gnu.org/licenses/>.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "cellrenderer_timetrack.h"

#include <valarray>

#include <gdkmm/general.h>

#include <synfig/layers/layer_pastecanvas.h>
#include <synfig/valuenodes/valuenode_animated.h>
#include <synfig/valuenodes/valuenode_dynamiclist.h>

#include <gui/exception_guard.h>
#include <gui/instance.h>
#include <gui/waypointrenderer.h>

#endif

using namespace synfig;
using namespace synfigapp;
using namespace studio;

/* === M A C R O S ========================================================= */

/* === G L O B A L S ======================================================= */

//mode for modifier keys
namespace {
	enum {
		NONE = 0,
		SELECT_MASK = Gdk::CONTROL_MASK,
		COPY_MASK = Gdk::SHIFT_MASK,
		DELETE_MASK = Gdk::MOD1_MASK
	};
};

/* === P R O C E D U R E S ================================================= */

static Time
get_time_offset_from_vdesc(const ValueDesc &v)
{
#ifdef ADJUST_WAYPOINTS_FOR_TIME_OFFSET
	if (v.get_value_type() != type_canvas || getenv("SYNFIG_SHOW_CANVAS_PARAM_WAYPOINTS"))
		return Time::zero();

	if (!v.get_value().get(Canvas::Handle()))
		return Time::zero();

	if (!v.parent_is_layer())
		return Time::zero();

	synfig::Layer::Handle layer = v.get_layer();

	if (!etl::handle<Layer_PasteCanvas>::cast_dynamic(layer))
		return Time::zero();

	return layer->get_param("time_offset").get(Time());
#else // ADJUST_WAYPOINTS_FOR_TIME_OFFSET
	return synfig::Time::zero();
#endif
}

static Time
get_time_dilation_from_vdesc(const ValueDesc &v)
{
#ifdef ADJUST_WAYPOINTS_FOR_TIME_OFFSET
	if (v.get_value_type() != type_canvas || getenv("SYNFIG_SHOW_CANVAS_PARAM_WAYPOINTS"))
		return Time(1.0);

	if (!v.get_value().get(Canvas::Handle()))
		return Time(1.0);

	if (!v.parent_is_layer())
		return Time(1.0);

	Layer::Handle layer = v.get_layer();

	if (!etl::handle<Layer_PasteCanvas>::cast_dynamic(layer))
		return Time(1.0);

	return layer->get_param("time_dilation").get(Time());
#else // ADJUST_WAYPOINTS_FOR_TIME_OFFSET
	return Time(1.0);
#endif
}

// kind of a hack... pointer is ugly
static const Node::time_set*
get_times_from_vdesc(const ValueDesc &v)
{
	if (v.get_value_type() == type_canvas && !getenv("SYNFIG_SHOW_CANVAS_PARAM_WAYPOINTS"))
		if(Canvas::Handle canvasparam = v.get_value().get(Canvas::Handle()))
			return &canvasparam->get_times();

	//we want a dynamic list entry to override the normal...
	if (v.parent_is_value_node())
		if (ValueNode_DynamicList *parent_value_node = dynamic_cast<ValueNode_DynamicList *>(v.get_parent_value_node().get()))
			return &parent_value_node->list[v.get_index()].get_times();

	if (ValueNode *base_value = v.get_value_node().get()) //don't render stuff if it's just animated...
		return &base_value->get_times();

	return 0;
}

static void
get_change_times_from_vdesc(const ValueDesc &v, std::set<Time> &out_times)
{
	if (v.is_value_node()) {
		if ( v.get_value_type() == type_string
		  || v.get_value_type() == type_bool
		  || v.get_value_type() == type_canvas )
		{
			v.get_value_node()->get_value_change_times(out_times);
		}
	}
}

static bool
get_closest_time(const Node::time_set &tset, const Time &t, const Time &scope, Time &out)
{
	Node::time_set::const_iterator	i,j,end = tset.end();

	// stop the crash mentioned in bug #1689282
	// doesn't solve the underlying problem though, I don't think
	if (tset.size() == 0) {
		error(__FILE__":%d: tset.size() == 0",__LINE__);
		return false;
	}

	// TODO: add in RangeGet so it's not so damn hard to click on points
	i = tset.upper_bound(t); //where t is the lower bound, t < [first,i)
	j = i; --j;

	double dist = Time::end();
	double closest = 0;

	if (i != end) {
		closest = i->get_time();
		dist = fabs(i->get_time() - t);
	}

	if (j != end && fabs(j->get_time() - t) < dist) {
		closest = j->get_time();
		dist = fabs(j->get_time() - t);
	}

	if (dist <= scope) {
		out = closest;
		return true;
	}

	return false;
}

static UniqueID
find_editable_waypoint(const ValueDesc &v, const Time &t, const Time& scope = Time::end())
{
	if (ValueNode_Animated::Handle value_node_animated = ValueNode_Animated::Handle::cast_dynamic(v.get_value_node())) {
		UniqueID id;
	    Time nearest = Time::end();
		ValueNode_Animated::WaypointList &list = value_node_animated->editable_waypoint_list();
		for(ValueNode_Animated::WaypointList::iterator i = list.begin(); i != list.end(); ++i) {
			Time val = fabs((double)(i->get_time() - t));
			if (val < nearest)
				{ nearest = val; id = *i; }
		}
		if (nearest != Time::end() && nearest < scope)
			return id;
	}
	return UniqueID::nil();
}

/* === M E T H O D S ======================================================= */

CellRenderer_TimeTrack::CellRenderer_TimeTrack():
	Glib::ObjectBase	(typeid(CellRenderer_TimeTrack)),
	time_plot_data		(nullptr),
	mode				(),
	dragging			(false),
	property_valuedesc_	(*this, "value_desc", ValueDesc()),
	property_canvas_	(*this, "canvas", Canvas::Handle())
	{ }

CellRenderer_TimeTrack::~CellRenderer_TimeTrack()
	{ }

void
CellRenderer_TimeTrack::set_time_model(const etl::handle<TimeModel> &x)
	{ time_plot_data.set_time_model(x); }

void
CellRenderer_TimeTrack::set_canvas_interface(const etl::loose_handle<CanvasInterface> &x)
	{ canvas_interface = x; }

// The following two functions don't get documented correctly by
// doxygen 1.5.[23] because of a bug with any function whose name
// begins with 'property'.  Fixed in doxygen 1.5.4 apparently.  See
// http://bugzilla.gnome.org/show_bug.cgi?id=471185 .
Glib::PropertyProxy<ValueDesc>
CellRenderer_TimeTrack::property_value_desc()
	{ return Glib::PropertyProxy<ValueDesc>(this,"value_desc"); }

Glib::PropertyProxy<Canvas::Handle>
CellRenderer_TimeTrack::property_canvas()
	{ return Glib::PropertyProxy<Canvas::Handle>(this,"canvas"); }

void
CellRenderer_TimeTrack::render_vfunc(
	const ::Cairo::RefPtr< ::Cairo::Context>& cr,
	Gtk::Widget& /* widget */,
	const Gdk::Rectangle& /* background_area */,
	const Gdk::Rectangle& cell_area,
	Gtk::CellRendererState /* flags */)
{
	if (!cr || cell_area.get_width() <= 0 || cell_area.get_height() <= 0 || !time_plot_data.time_model)
		return;

	Gdk::RGBA change_time_color("#888888");
	Gdk::RGBA curr_time_color("#0000ff");
	Gdk::RGBA inactive_color("rgba(0.,0.,0.,0.5)");
	Gdk::RGBA keyframe_color("#a07f7f");
	Gdk::RGBA activepoint_color[] = {
		Gdk::RGBA("#ff0000"),
		Gdk::RGBA("#00ff00") };

	Time time = time_plot_data.time_model->get_time();
	if (time_plot_data.is_invalid())
		return;

	time_plot_data.set_dimensions(Gdk::Point(cell_area.get_width(), cell_area.get_height()));

	Canvas::Handle canvas = property_canvas().get_value();
	ValueDesc value_desc = property_value_desc().get_value();
	ValueNode::Handle base_value = value_desc.get_value_node();

	// If the canvas is defined, then load up the keyframes
	if (canvas)
		for(KeyframeList::const_iterator i = canvas->keyframe_list().begin(); i != canvas->keyframe_list().end(); ++i)
			if (time_plot_data.is_time_visible_extra(i->get_time())) {
				int x = time_plot_data.get_pixel_t_coord(i->get_time());
				Gdk::Cairo::set_source_rgba(cr, keyframe_color);
				cr->rectangle(cell_area.get_x() + x, cell_area.get_y(), 1, cell_area.get_height());
				cr->fill();
			}

	Time time_offset = get_time_offset_from_vdesc(value_desc);
	Time time_dilation = get_time_dilation_from_vdesc(value_desc);
	double time_k = time_dilation == Time::zero() ? 1.0 : 1.0/(double)time_dilation;

	//render time points where value changed
	{
		std::set<Time> times;
		get_change_times_from_vdesc(value_desc, times);
		const double yc = cell_area.get_y() + cell_area.get_height()/2.;
		cr->set_source_rgb(
		    change_time_color.get_red(),
		    change_time_color.get_green(),
		    change_time_color.get_blue() );
		for(std::set<Time>::const_iterator i = times.begin(); i != times.end(); ++i) {
			//find the coordinate in the drawable space...
			Time t = (*i - time_offset)*time_k;
			if (time_plot_data.is_time_visible_extra(t)) {
				cr->arc(cell_area.get_x()+1+time_plot_data.get_pixel_t_coord(*i), yc, 2, 0, 2*3.1415);
				cr->fill();
			}
		}
	}

	// if this valuedesc is not animated and has waypoints, those waypoints are probably from inner value nodes ( = converted)
	const bool is_static_value_node = (value_desc.is_value_node() && !synfig::ValueNode_Animated::Handle::cast_dynamic(value_desc.get_value_node()));

	// render all the time points that exist
	if (const Node::time_set *tset = get_times_from_vdesc(value_desc)) {
		bool valselected = sel_value.get_value_node() == base_value && !sel_times.empty();
		float cfps = get_canvas()->rend_desc().get_frame_rate();
		Time diff = actual_time - actual_dragtime;
		if (cfps) diff = (actual_time - actual_dragtime).round(cfps);

		std::vector<TimePoint> drawredafter;
		for(Node::time_set::const_iterator i = tset->begin(); i != tset->end(); ++i) {
			// find the coordinate in the drawable space...
			Time t = (i->get_time() - time_offset)*time_k;
			if (time_plot_data.is_time_visible_extra(t)) {
				// if it found it... (might want to change comparison, and optimize
				//                    sel_times.find to not produce an overall nlogn solution)

				bool selected=false;

				// not dragging... just draw as per normal
				// if move dragging draw offset
				// if copy dragging draw both...
				if (valselected && sel_times.count(i->get_time())) {
					if (dragging) { //skip if we're dragging because we'll render it later
						if (mode & COPY_MASK) {
							// draw both blue and red moved
							drawredafter.push_back(*i);
							drawredafter.back().set_time(t + diff);
						} else
						if (mode & DELETE_MASK) {
							// it's just red...
							selected = true;
						} else {
							// move - draw the red on top of the others...
							drawredafter.push_back(*i);
							drawredafter.back().set_time(t + diff);
							continue;
						}
					} else selected = true;
				}

				// should draw me a grey filled circle...
				int x = time_plot_data.get_pixel_t_coord(t);
				Gdk::Rectangle area(
					cell_area.get_x() - cell_area.get_height()/2 + x + 1,
					cell_area.get_y() + 1,
					cell_area.get_height() - 2,
					cell_area.get_height() - 2 );
				TimePoint tp_copy = *i;
				tp_copy.set_time(t);
				WaypointRenderer::render_time_point_to_window(cr, area, tp_copy, selected, false, is_static_value_node);
			}
		}

		for(std::vector<TimePoint>::iterator i = drawredafter.begin(); i != drawredafter.end(); ++i) {
			int x = time_plot_data.get_pixel_t_coord(i->get_time());
			Gdk::Rectangle area(
				cell_area.get_x() - cell_area.get_height()/2 + x + 1,
				cell_area.get_y() + 1,
				cell_area.get_height() - 2,
				cell_area.get_height() - 2 );
			WaypointRenderer::render_time_point_to_window(cr, area, *i, true, false, is_static_value_node);
		}
	}

	// If the parent of this value node is a dynamic list, then
	// render the on and off times
	ValueNode_DynamicList::Handle parent_dynamic_list_value_node;
	if (value_desc.parent_is_value_node())
		parent_dynamic_list_value_node = ValueNode_DynamicList::Handle::cast_dynamic(value_desc.get_parent_value_node());
	if (parent_dynamic_list_value_node) {
		const ValueNode_DynamicList::ListEntry& list_entry = parent_dynamic_list_value_node->list[ value_desc.get_index() ];
		const ValueNode_DynamicList::ListEntry::ActivepointList& activepoint_list = list_entry.timing_info;

		synfig::Time previous_activepoint_time = synfig::Time::begin();
		synfig::Time initial_off_time = list_entry.status_at_time(previous_activepoint_time) ? synfig::Time::end() : synfig::Time::begin();

		for (const auto& activepoint : activepoint_list) {
			const int w = selected == activepoint ? 3 : 1;

			if (!list_entry.status_at_time(0.5*(previous_activepoint_time + activepoint.get_time()))) {
				if (initial_off_time == synfig::Time::end())
					initial_off_time = previous_activepoint_time;
			} else {
				if (initial_off_time != synfig::Time::end()) {
					cr->set_source_rgba(inactive_color.get_red(), inactive_color.get_green(), inactive_color.get_blue(), inactive_color.get_alpha());
					cr->rectangle(cell_area.get_x() + w/2+time_plot_data.get_pixel_t_coord(initial_off_time), cell_area.get_y(), -w+time_plot_data.get_delta_pixel_from_delta_t_coord(previous_activepoint_time - initial_off_time), cell_area.get_height());
					cr->fill();
					initial_off_time = synfig::Time::end();
				}
			}
			previous_activepoint_time = activepoint.get_time();

			if (time_plot_data.is_time_visible_extra(activepoint.get_time())) {
				Gdk::Cairo::set_source_rgba(cr, activepoint_color[activepoint.get_state() ? 1 : 0]);
				cr->rectangle(
				    cell_area.get_x() + time_plot_data.get_pixel_t_coord(activepoint.get_time()) - w/2,
				    cell_area.get_y(),
				    w,
				    cell_area.get_height() );
				cr->fill();
			}
		}

		if (initial_off_time != synfig::Time::end() || !list_entry.status_at_time(synfig::Time::end())) {
			initial_off_time = synfig::clamp(initial_off_time, time_plot_data.lower, previous_activepoint_time);
			int initial_pixel_index = cell_area.get_x() + /*+w/2*/+time_plot_data.get_pixel_t_coord(initial_off_time);
			cr->set_source_rgba(inactive_color.get_red(), inactive_color.get_green(), inactive_color.get_blue(), inactive_color.get_alpha());
			cr->rectangle(initial_pixel_index, cell_area.get_y(), cell_area.get_width()/* - initial_pixel_index*/, cell_area.get_height());
			cr->fill();
		}
	}

	// Render a line that defines the current tick in time
	if (time_plot_data.is_time_visible_extra(time)) {
		int x = time_plot_data.get_pixel_t_coord(time);
		Gdk::Cairo::set_source_rgba(cr, curr_time_color);
		cr->rectangle(cell_area.get_x() + x, cell_area.get_y(), 1, cell_area.get_height());
		cr->fill();
	}
}

bool
CellRenderer_TimeTrack::activate_vfunc(
	GdkEvent* event,
	Gtk::Widget& /*widget*/,
	const Glib::ustring& /*treepath*/,
	const Gdk::Rectangle& /*background_area*/,
	const Gdk::Rectangle& cell_area,
	Gtk::CellRendererState /*flags*/)
{
	SYNFIG_EXCEPTION_GUARD_BEGIN()
	// Catch a null event received us a result of a keypress (only?)
	if (!event)
		return true; // On tab key press, Focus go to next panel. If return false, focus goes to canvas

	if (cell_area.get_width() <= 0 || cell_area.get_height() <= 0 || !time_plot_data.time_model)
		return false;

	if (time_plot_data.is_invalid())
		return false;

	switch(event->type) {
	case GDK_MOTION_NOTIFY:
		actual_time = time_plot_data.get_t_from_pixel_coord(event->motion.x - (double)cell_area.get_x());
		break;
	case GDK_2BUTTON_PRESS:
	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
		actual_time = time_plot_data.get_t_from_pixel_coord(event->button.x - (double)cell_area.get_x());
		break;
	default:
		return false;
	}
	actual_time = std::max(actual_time, time_plot_data.time_model->get_lower());

	Gdk::ModifierType gdk_mode;
	Gdk::Event(event).get_state(gdk_mode);
	mode = gdk_mode;

	float fps = 0.f;
	Time selected_time = actual_time;
	if (Canvas::Handle canvas = get_canvas()) {
		fps = canvas->rend_desc().get_frame_rate();
		if (approximate_less_or_equal_lp(fps, 0.f)) fps = 0.f;
		if (fps)
			selected_time = selected_time.round(fps);
	}

	ValueDesc value_desc = property_value_desc().get_value();
	Time time_offset = get_time_offset_from_vdesc(value_desc);
	Time time_dilation = get_time_dilation_from_vdesc(value_desc);

    switch(event->type) {
	case GDK_MOTION_NOTIFY:
		return true;
	case GDK_2BUTTON_PRESS:
	case GDK_BUTTON_PRESS: {
		//Deal with time point selection, but only if they aren't involved in the insanity...
		Time stime;
		const Node::time_set *tset = get_times_from_vdesc(value_desc);
		const int waypoint_half_width = cell_area.get_height()/2;
		// the time span a waypoint image "lasts"
		const Time waypoint_half_width_time = time_plot_data.get_delta_t_from_delta_pixel_coord(waypoint_half_width);

		bool clickfound = tset && get_closest_time(*tset, actual_time*time_dilation + time_offset, waypoint_half_width_time, stime);

		selected = find_editable_waypoint(value_desc, selected_time, waypoint_half_width_time);

		if (event->button.button == 1) {
			//  UI specification:
			//
			//  When nothing is selected, clicking on a point in either normal mode or
			//      additive mode will select the time point closest to the click.
			//      Subtractive click will do nothing
			//
			//  When things are already selected, clicking on a selected point does
			//      nothing (in both normal and add mode).  Add mode clicking on an unselected
			//      point adds it to the set.  Normal clicking on an unselected point will
			//      select only that one time point.  Subtractive clicking on any point
			//      will remove it from the the set if it is included.

			bool selectmode = mode & SELECT_MASK;

			// NOTE LATER ON WE SHOULD MAKE IT SO MULTIPLE VALUENODES CAN BE SELECTED AT ONCE
			// we want to jump to the value desc if we're not currently on it
			//	 but only if we want to add the point
			if (clickfound && sel_value != value_desc) {
				sel_value = value_desc;
				sel_times.clear();
			}

			//now that we've made sure we're selecting the correct value, deal with the already selected points
			std::set<Time>::iterator foundi = clickfound ? sel_times.find(stime) : sel_times.end();
			bool found = foundi != sel_times.end();

			if (!selectmode && !found) {
				// remove all other points from our list... (only select the one we need)
				sel_times.clear();
			}

			if (found && selectmode) {
				// remove a single already selected point
				sel_times.erase(foundi);
			} else
			if (clickfound) {
				//otherwise look at adding it
				//for replace the list was cleared earlier, and for add it wasn't so it works
				sel_times.insert(stime);
				if (sel_times.size() == 1 && event->type == GDK_2BUTTON_PRESS) {
					ValueBase v = value_desc.get_value(stime);
					etl::handle<Node> node = v.get_type() == type_canvas && !getenv("SYNFIG_SHOW_CANVAS_PARAM_WAYPOINTS")
					               ? etl::handle<Node>(v.get(Canvas::Handle()))
					               : etl::handle<Node>(value_desc.get_value_node());
					if (node)
						signal_waypoint_clicked_cellrenderer()(node, stime, time_offset, time_dilation, -1);
				} 
				
			}

			if (selected || !sel_times.empty()) {
				dragging = true;
				actual_dragtime = actual_time;
			}
		} else
		if (event->button.button == 3) {
			ValueBase v = value_desc.get_value(stime);
			etl::handle<Node> node = v.get_type() == type_canvas && !getenv("SYNFIG_SHOW_CANVAS_PARAM_WAYPOINTS")
					               ? etl::handle<Node>(v.get(Canvas::Handle()))
					               : etl::handle<Node>(value_desc.get_value_node());
			if (clickfound && node)
				signal_waypoint_clicked_cellrenderer()(node, stime, time_offset, time_dilation, 2); // button was replaced
		}

		return true;
	}
	case GDK_BUTTON_RELEASE: {
		dragging = false;

		//Time point stuff...
		if (event->button.button == 1) {
			bool delete_mode = (mode & DELETE_MASK) && !(mode & COPY_MASK);
			Time deltatime = (actual_time - actual_dragtime)*time_dilation;
			if (sel_times.size() != 0 && (delete_mode || deltatime != Time())) {
				Action::ParamList param_list;
				param_list.add("canvas", canvas_interface->get_canvas());
				param_list.add("canvas_interface", canvas_interface);
				if (sel_value.get_value_type() == type_canvas && !getenv("SYNFIG_SHOW_CANVAS_PARAM_WAYPOINTS")) {
					param_list.add("addcanvas", sel_value.get_value().get(Canvas::Handle()));
				} else {
					param_list.add("addvaluedesc", sel_value);
				}

				std::set<Time> newset;
				for(std::set<Time>::iterator i = sel_times.begin(); i != sel_times.end(); ++i) {
					param_list.add("addtime", *i);
					Time t = *i + deltatime;
					if (fps) t = t.round(fps);
					newset.insert(t);
				}

				if (!delete_mode)
					param_list.add("deltatime", deltatime);

				if (mode & COPY_MASK) { // copy
					etl::handle<studio::Instance>::cast_dynamic(canvas_interface->get_instance())
						->process_action("TimepointsCopy", param_list);
				} else
				if (delete_mode) { // delete
					etl::handle<studio::Instance>::cast_dynamic(canvas_interface->get_instance())
						->process_action("TimepointsDelete", param_list);
				} else { // move
					etl::handle<studio::Instance>::cast_dynamic(canvas_interface->get_instance())
						->process_action("TimepointsMove", param_list);
				}

				//now replace all the selected with the new selected
				sel_times = newset;
			}
		}

		return true;
	}
	default:
		break;
	}

	return false;
	SYNFIG_EXCEPTION_GUARD_END_BOOL(true)
}
