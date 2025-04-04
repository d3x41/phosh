/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-overview"

#include "phosh-config.h"

#include "activity.h"
#include "app-grid-button.h"
#include "app-grid.h"
#include "overview.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "phosh-private-client-protocol.h"
#include "phosh-wayland.h"
#include "shell-priv.h"
#include "toplevel-manager.h"
#include "toplevel-thumbnail.h"
#include "util.h"

#include <gio/gdesktopappinfo.h>

#include <handy.h>

#define OVERVIEW_ICON_SIZE 64

/**
 * PhoshOverview:
 *
 * The overview shows running apps and the app grid to launch new
 * applications.
 *
 * The #PhoshOverview shows running apps (#PhoshActivity) and
 * the app grid (#PhoshAppGrid) to launch new applications.
 */

enum {
  ACTIVITY_LAUNCHED,
  ACTIVITY_RAISED,
  ACTIVITY_CLOSED,
  SELECTION_ABORTED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

enum {
  PROP_0,
  PROP_HAS_ACTIVITIES,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


typedef struct
{
  /* Running activities */
  GtkWidget *carousel_running_activities;
  GtkWidget *app_grid;
  PhoshActivity *activity;

  int       has_activities;
} PhoshOverviewPrivate;


struct _PhoshOverview
{
  GtkBoxClass parent;
};

G_DEFINE_TYPE_WITH_PRIVATE (PhoshOverview, phosh_overview, GTK_TYPE_BOX)


static void
phosh_overview_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PhoshOverview *self = PHOSH_OVERVIEW (object);
  PhoshOverviewPrivate *priv = phosh_overview_get_instance_private (self);

  switch (property_id) {
  case PROP_HAS_ACTIVITIES:
    g_value_set_boolean (value, priv->has_activities);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static PhoshToplevel *
get_toplevel_from_activity (PhoshActivity *activity)
{
  PhoshToplevel *toplevel;
  g_return_val_if_fail (PHOSH_IS_ACTIVITY (activity), NULL);
  toplevel = g_object_get_data (G_OBJECT (activity), "toplevel");
  g_return_val_if_fail (PHOSH_IS_TOPLEVEL (toplevel), NULL);

  return toplevel;
}


static PhoshActivity *
find_activity_by_toplevel (PhoshOverview        *self,
                           PhoshToplevel        *needle)
{
  g_autoptr(GList) children;
  PhoshActivity *activity = NULL;
  PhoshOverviewPrivate *priv = phosh_overview_get_instance_private (self);

  children = gtk_container_get_children (GTK_CONTAINER (priv->carousel_running_activities));
  for (GList *l = children; l; l = l->next) {
    PhoshToplevel *toplevel;

    activity = PHOSH_ACTIVITY (l->data);
    toplevel = get_toplevel_from_activity (activity);
    if (toplevel == needle)
      break;
  }

  g_return_val_if_fail (activity, NULL);
  return activity;
}


static void
scroll_to_activity (PhoshOverview *self, PhoshActivity *activity)
{
  PhoshOverviewPrivate *priv = phosh_overview_get_instance_private (self);
  hdy_carousel_scroll_to (HDY_CAROUSEL (priv->carousel_running_activities), GTK_WIDGET (activity));
  gtk_widget_grab_focus (GTK_WIDGET (activity));
}

static void
on_activity_clicked (PhoshOverview *self, PhoshActivity *activity)
{
  PhoshToplevel *toplevel;
  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  g_return_if_fail (PHOSH_IS_ACTIVITY (activity));

  toplevel = get_toplevel_from_activity (activity);
  g_return_if_fail (toplevel);

  g_debug("Will raise %s (%s)",
          phosh_activity_get_app_id (activity),
          phosh_toplevel_get_title (toplevel));

  phosh_toplevel_activate (toplevel, phosh_wayland_get_wl_seat (phosh_wayland_get_default ()));
  g_signal_emit (self, signals[ACTIVITY_RAISED], 0);
}


static void
on_activity_closed (PhoshOverview *self, PhoshActivity *activity)
{
  PhoshToplevel *toplevel;

  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  g_return_if_fail (PHOSH_IS_ACTIVITY (activity));

  toplevel = g_object_get_data (G_OBJECT (activity), "toplevel");
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));

  g_debug ("Will close %s (%s)",
           phosh_activity_get_app_id (activity),
           phosh_toplevel_get_title (toplevel));

  phosh_toplevel_close (toplevel);
  phosh_trigger_feedback ("window-close");
  g_signal_emit (self, signals[ACTIVITY_CLOSED], 0);
}


static void
on_activity_fullscreened (PhoshOverview *self, gboolean fullscreen, PhoshActivity *activity)
{
  PhoshToplevel *toplevel;

  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  g_return_if_fail (PHOSH_IS_ACTIVITY (activity));

  toplevel = g_object_get_data (G_OBJECT (activity), "toplevel");
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));

  g_debug ("Fullscreen %s (%s); %d",
           phosh_activity_get_app_id (activity),
           phosh_toplevel_get_title (toplevel),
           fullscreen);

  phosh_toplevel_fullscreen (toplevel, fullscreen);
}


static void
on_toplevel_closed (PhoshToplevel *toplevel, PhoshOverview *overview)
{
  PhoshActivity *activity;
  PhoshOverviewPrivate *priv;

  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));
  g_return_if_fail (PHOSH_IS_OVERVIEW (overview));
  priv = phosh_overview_get_instance_private (overview);

  activity = find_activity_by_toplevel (overview, toplevel);
  g_return_if_fail (PHOSH_IS_ACTIVITY (activity));
  gtk_widget_destroy (GTK_WIDGET (activity));

  if (priv->activity == activity)
    priv->activity = NULL;
}


static void
on_toplevel_activated_changed (PhoshToplevel *toplevel, GParamSpec *pspec, PhoshOverview *overview)
{
  PhoshActivity *activity;
  PhoshOverviewPrivate *priv;
  g_return_if_fail (PHOSH_IS_OVERVIEW (overview));
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));
  priv = phosh_overview_get_instance_private (overview);

  if (phosh_toplevel_is_activated (toplevel)) {
    activity = find_activity_by_toplevel (overview, toplevel);
    priv->activity = activity;
    scroll_to_activity (overview, activity);
  }
}


static void
on_thumbnail_ready_changed (PhoshThumbnail *thumbnail, GParamSpec *pspec, PhoshActivity *activity)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL (thumbnail));
  g_return_if_fail (PHOSH_IS_ACTIVITY (activity));

  phosh_activity_set_thumbnail (activity, thumbnail);
}


static void
request_thumbnail (PhoshActivity *activity, PhoshToplevel *toplevel)
{
  PhoshToplevelThumbnail *thumbnail;
  GtkAllocation allocation;
  int scale;
  g_return_if_fail (PHOSH_IS_ACTIVITY (activity));
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));
  scale = gtk_widget_get_scale_factor (GTK_WIDGET (activity));
  phosh_activity_get_thumbnail_allocation (activity, &allocation);
  thumbnail = phosh_toplevel_thumbnail_new_from_toplevel (toplevel, allocation.width * scale, allocation.height * scale);
  g_signal_connect_object (thumbnail, "notify::ready", G_CALLBACK (on_thumbnail_ready_changed), activity, 0);
}


static void
on_activity_resized (PhoshActivity *activity, GtkAllocation *alloc, PhoshToplevel *toplevel)
{
  request_thumbnail (activity, toplevel);
}


static void
on_activity_has_focus_changed (PhoshOverview *self, GParamSpec *pspec, PhoshActivity *activity)
{
  PhoshOverviewPrivate *priv;

  g_return_if_fail (PHOSH_IS_ACTIVITY (activity));
  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  priv = phosh_overview_get_instance_private (self);

  if (gtk_widget_has_focus (GTK_WIDGET (activity)))
    hdy_carousel_scroll_to (HDY_CAROUSEL (priv->carousel_running_activities), GTK_WIDGET (activity));
}


static int
get_last_app_id_pos (PhoshOverview *self, const char *app_id)
{
  PhoshOverviewPrivate *priv;
  g_autoptr (GList) children = NULL;
  int pos;

  if (!app_id)
    return 0;

  priv = phosh_overview_get_instance_private (self);

  children = gtk_container_get_children (GTK_CONTAINER (priv->carousel_running_activities));
  pos = g_list_length (children);
  for (GList *l = g_list_last (children); l; l = l->prev) {
    PhoshActivity *a = PHOSH_ACTIVITY (l->data);

    if (g_strcmp0 (phosh_activity_get_app_id (a), app_id) == 0)
      break;

    pos--;
  }

  return pos;
}


static void
add_activity (PhoshOverview *self, PhoshToplevel *toplevel)
{
  PhoshOverviewPrivate *priv;
  GtkWidget *activity;
  const char *app_id, *title;
  const char *parent_app_id = NULL;
  int width, height;
  PhoshToplevelManager *m = phosh_shell_get_toplevel_manager (phosh_shell_get_default ());
  PhoshToplevel *parent = NULL;
  gint pos;

  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  priv = phosh_overview_get_instance_private (self);

  app_id = phosh_toplevel_get_app_id (toplevel);
  title = phosh_toplevel_get_title (toplevel);

  if (phosh_toplevel_get_parent_handle (toplevel))
    parent = phosh_toplevel_manager_get_parent (m, toplevel);
  if (parent)
    parent_app_id = phosh_toplevel_get_app_id (parent);

  g_debug ("Building activator for '%s' (%s)", app_id, title);
  phosh_shell_get_usable_area (phosh_shell_get_default (), NULL, NULL, &width, &height);
  activity = g_object_new (PHOSH_TYPE_ACTIVITY,
                           "app-id", app_id,
                           "parent-app-id", parent_app_id,
                           "win-width", width,
                           "win-height", height,
                           "maximized", phosh_toplevel_is_maximized (toplevel),
                           "fullscreen", phosh_toplevel_is_fullscreen (toplevel),
                           NULL);
  g_object_set_data (G_OBJECT (activity), "toplevel", toplevel);

  pos = get_last_app_id_pos (self, parent_app_id);
  if (pos)
    hdy_carousel_insert (HDY_CAROUSEL (priv->carousel_running_activities), activity, pos);
  else
    gtk_container_add (GTK_CONTAINER (priv->carousel_running_activities), activity);
  gtk_widget_set_visible (activity, TRUE);

  g_object_connect (activity,
                    "swapped-signal::clicked", on_activity_clicked, self,
                    "swapped-signal::closed", on_activity_closed, self,
                    "swapped-signal::fullscreened", on_activity_fullscreened, self,
                    NULL);

  g_signal_connect_object (toplevel, "closed", G_CALLBACK (on_toplevel_closed), self, 0);
  g_signal_connect_object (toplevel, "notify::activated", G_CALLBACK (on_toplevel_activated_changed), self, 0);
  g_object_bind_property (toplevel, "maximized", activity, "maximized", G_BINDING_DEFAULT);
  g_object_bind_property (toplevel, "fullscreen", activity, "fullscreen", G_BINDING_DEFAULT);

  g_signal_connect (activity, "resized", G_CALLBACK (on_activity_resized), toplevel);
  g_signal_connect_swapped (activity, "notify::has-focus", G_CALLBACK (on_activity_has_focus_changed), self);

  if (phosh_toplevel_is_activated (toplevel)) {
    scroll_to_activity (self, PHOSH_ACTIVITY (activity));
    priv->activity = PHOSH_ACTIVITY (activity);
  }
}


static void
get_running_activities (PhoshOverview *self)
{
  PhoshOverviewPrivate *priv;
  PhoshToplevelManager *toplevel_manager = phosh_shell_get_toplevel_manager (phosh_shell_get_default ());
  guint toplevels_num = phosh_toplevel_manager_get_num_toplevels (toplevel_manager);
  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  priv = phosh_overview_get_instance_private (self);

  priv->has_activities = !!toplevels_num;
  if (toplevels_num == 0)
    gtk_widget_set_visible (priv->carousel_running_activities, FALSE);

  for (guint i = 0; i < toplevels_num; i++) {
    PhoshToplevel *toplevel = phosh_toplevel_manager_get_toplevel (toplevel_manager, i);
    add_activity (self, toplevel);
  }
}


static void
toplevel_added_cb (PhoshOverview        *self,
                   PhoshToplevel        *toplevel,
                   PhoshToplevelManager *manager)
{
  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (manager));
  add_activity (self, toplevel);
}


static void
toplevel_changed_cb (PhoshOverview        *self,
                     PhoshToplevel        *toplevel,
                     PhoshToplevelManager *manager)
{
  PhoshActivity *activity;

  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (manager));

  if (phosh_shell_get_state (phosh_shell_get_default ()) & PHOSH_STATE_OVERVIEW)
    return;

  activity = find_activity_by_toplevel (self, toplevel);
  g_return_if_fail (activity);

  request_thumbnail (activity, toplevel);
}


static void
num_toplevels_cb (PhoshOverview        *self,
                  GParamSpec           *pspec,
                  PhoshToplevelManager *manager)
{
  PhoshOverviewPrivate *priv;
  gboolean has_activities;

  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (manager));
  priv = phosh_overview_get_instance_private (self);

  has_activities = !!phosh_toplevel_manager_get_num_toplevels (manager);
  if (priv->has_activities == has_activities)
    return;

  priv->has_activities = has_activities;
  gtk_widget_set_visible (priv->carousel_running_activities, has_activities);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_ACTIVITIES]);
}


static void
phosh_overview_size_allocate (GtkWidget     *widget,
                              GtkAllocation *alloc)
{
  PhoshOverview *self = PHOSH_OVERVIEW (widget);
  PhoshOverviewPrivate *priv = phosh_overview_get_instance_private (self);
  g_autoptr (GList) children = NULL;
  int width, height;

  phosh_shell_get_usable_area (phosh_shell_get_default (), NULL, NULL, &width, &height);
  children = gtk_container_get_children (GTK_CONTAINER (priv->carousel_running_activities));

  for (GList *l = children; l; l = l->next) {
    g_object_set (l->data,
                  "win-width", width,
                  "win-height", height,
                  NULL);
  }

  GTK_WIDGET_CLASS (phosh_overview_parent_class)->size_allocate (widget, alloc);
}


static void
app_launched_cb (PhoshOverview *self,
                 GAppInfo      *info,
                 GtkWidget     *widget)
{
  g_return_if_fail (PHOSH_IS_OVERVIEW (self));

  g_signal_emit (self, signals[ACTIVITY_LAUNCHED], 0);
}


static void
page_changed_cb (PhoshOverview *self,
                 guint          index,
                 HdyCarousel   *carousel)
{
  PhoshActivity *activity;
  PhoshToplevel *toplevel;
  g_autoptr (GList) list = NULL;
  g_return_if_fail (PHOSH_IS_OVERVIEW (self));
  g_return_if_fail (HDY_IS_CAROUSEL (carousel));

  /* Carousel is empty */
  if (((int)index < 0))
    return;

  /* don't raise on scroll in docked mode */
  if (phosh_shell_get_docked (phosh_shell_get_default ()))
    return;

  /* ignore page changes when overview is not open */
  if (!(phosh_shell_get_state (phosh_shell_get_default ()) & PHOSH_STATE_OVERVIEW))
    return;

  list = gtk_container_get_children (GTK_CONTAINER (carousel));
  activity = PHOSH_ACTIVITY (g_list_nth_data (list, index));
  toplevel = get_toplevel_from_activity (activity);
  phosh_toplevel_activate (toplevel, phosh_wayland_get_wl_seat (phosh_wayland_get_default ()));

  if (!gtk_widget_has_focus (GTK_WIDGET (activity)))
    gtk_widget_grab_focus (GTK_WIDGET (activity));
}


static void
phosh_overview_constructed (GObject *object)
{
  PhoshOverview *self = PHOSH_OVERVIEW (object);
  PhoshOverviewPrivate *priv = phosh_overview_get_instance_private (self);
  PhoshToplevelManager *toplevel_manager =
      phosh_shell_get_toplevel_manager (phosh_shell_get_default ());

  G_OBJECT_CLASS (phosh_overview_parent_class)->constructed (object);

  g_signal_connect_object (toplevel_manager, "toplevel-added",
                           G_CALLBACK (toplevel_added_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (toplevel_manager, "toplevel-changed",
                           G_CALLBACK (toplevel_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (toplevel_manager, "notify::num-toplevels",
                           G_CALLBACK (num_toplevels_cb),
                           self,
                           G_CONNECT_SWAPPED);

  get_running_activities (self);

  g_signal_connect_swapped (priv->app_grid, "app-launched",
                            G_CALLBACK (app_launched_cb), self);

  g_signal_connect_swapped (priv->carousel_running_activities, "page-changed",
                            G_CALLBACK (page_changed_cb), self);
}


static void
phosh_overview_class_init (PhoshOverviewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = phosh_overview_constructed;
  object_class->get_property = phosh_overview_get_property;
  widget_class->size_allocate = phosh_overview_size_allocate;

  /**
   * PhoshOverview:has-activities:
   *
   * Whether the overview has running activities
   */
  props[PROP_HAS_ACTIVITIES] =
    g_param_spec_boolean ("has-activities", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  signals[ACTIVITY_LAUNCHED] =
    g_signal_new ("activity-launched",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[ACTIVITY_RAISED] =
    g_signal_new ("activity-raised",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[SELECTION_ABORTED] =
    g_signal_new ("selection-aborted",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[ACTIVITY_CLOSED] =
    g_signal_new ("activity-closed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /* ensure used custom types */
  PHOSH_TYPE_APP_GRID;
  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/overview.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PhoshOverview, app_grid);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshOverview,
                                                carousel_running_activities);

  gtk_widget_class_set_css_name (widget_class, "phosh-overview");
}


static void
phosh_overview_init (PhoshOverview *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


GtkWidget *
phosh_overview_new (void)
{
  return g_object_new (PHOSH_TYPE_OVERVIEW, NULL);
}


void
phosh_overview_refresh (PhoshOverview *self)
{
  PhoshOverviewPrivate *priv;
  g_return_if_fail(PHOSH_IS_OVERVIEW (self));
  priv = phosh_overview_get_instance_private (self);

  if (priv->activity) {
    gtk_widget_grab_focus (GTK_WIDGET (priv->activity));
    request_thumbnail (priv->activity, get_toplevel_from_activity (priv->activity));
  }
}


void
phosh_overview_reset (PhoshOverview *self)
{
  PhoshOverviewPrivate *priv;

  g_return_if_fail(PHOSH_IS_OVERVIEW (self));
  priv = phosh_overview_get_instance_private (self);

  phosh_app_grid_reset (PHOSH_APP_GRID (priv->app_grid));
}


void
phosh_overview_focus_app_search (PhoshOverview *self)
{
  PhoshOverviewPrivate *priv;

  g_return_if_fail(PHOSH_IS_OVERVIEW (self));
  priv = phosh_overview_get_instance_private (self);
  phosh_app_grid_focus_search (PHOSH_APP_GRID (priv->app_grid));
}


gboolean
phosh_overview_handle_search (PhoshOverview *self, GdkEvent *event)
{
  PhoshOverviewPrivate *priv;

  g_return_val_if_fail(PHOSH_IS_OVERVIEW (self), GDK_EVENT_PROPAGATE);
  priv = phosh_overview_get_instance_private (self);
  return phosh_app_grid_handle_search (PHOSH_APP_GRID (priv->app_grid), event);
}


gboolean
phosh_overview_has_running_activities (PhoshOverview *self)
{
  PhoshOverviewPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_OVERVIEW (self), FALSE);
  priv = phosh_overview_get_instance_private (self);

  return priv->has_activities;
}

/**
 * phosh_overview_get_app_grid:
 * @self: The overview
 *
 * Get the application grid
 *
 * Returns:(transfer none): The app grid widget
 */
PhoshAppGrid *
phosh_overview_get_app_grid (PhoshOverview *self)
{
  PhoshOverviewPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_OVERVIEW (self), NULL);
  priv = phosh_overview_get_instance_private (self);

  return PHOSH_APP_GRID (priv->app_grid);
}
