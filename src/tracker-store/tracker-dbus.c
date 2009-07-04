/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-utils.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-status.h>

#include <libtracker-db/tracker-db-dbus.h>
#include <libtracker-db/tracker-db-manager.h>

#include <libtracker-data/tracker-data-manager.h>
#include <libtracker-data/tracker-data-query.h>

#include "tracker-dbus.h"
#include "tracker-daemon.h"
#include "tracker-daemon-glue.h"
#include "tracker-resources.h"
#include "tracker-resources-glue.h"
#include "tracker-resource-class.h"
#include "tracker-resources-class-glue.h"
#include "tracker-search.h"
#include "tracker-search-glue.h"
#include "tracker-backup.h"
#include "tracker-backup-glue.h"
#include "tracker-marshal.h"
#include "tracker-main.h"

static DBusGConnection *connection;
static DBusGProxy      *gproxy;
static GSList	       *objects;

static gboolean
dbus_register_service (DBusGProxy  *proxy,
		       const gchar *name)
{
	GError *error = NULL;
	guint	result;

	g_message ("Registering DBus service...\n"
		   "  Name:'%s'",
		   name);

	if (!org_freedesktop_DBus_request_name (proxy,
						name,
						DBUS_NAME_FLAG_DO_NOT_QUEUE,
						&result, &error)) {
		g_critical ("Could not aquire name:'%s', %s",
			    name,
			    error ? error->message : "no error given");
		g_error_free (error);

		return FALSE;
	}

	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_critical ("DBus service name:'%s' is already taken, "
			    "perhaps the daemon is already running?",
			    name);
		return FALSE;
	}

	return TRUE;
}

static void
dbus_register_object (DBusGConnection	    *lconnection,
		      DBusGProxy	    *proxy,
		      GObject		    *object,
		      const DBusGObjectInfo *info,
		      const gchar	    *path)
{
	g_message ("Registering DBus object...");
	g_message ("  Path:'%s'", path);
	g_message ("  Type:'%s'", G_OBJECT_TYPE_NAME (object));

	dbus_g_object_type_install_info (G_OBJECT_TYPE (object), info);
	dbus_g_connection_register_g_object (lconnection, path, object);
}

static gboolean
dbus_register_names (TrackerConfig *config)
{
	GError *error = NULL;

	if (connection) {
		g_critical ("The DBusGConnection is already set, have we already initialized?");
		return FALSE;
	}

	if (gproxy) {
		g_critical ("The DBusGProxy is already set, have we already initialized?");
		return FALSE;
	}

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

	if (!connection) {
		g_critical ("Could not connect to the DBus session bus, %s",
			    error ? error->message : "no error given.");
		g_clear_error (&error);
		return FALSE;
	}

	/* The definitions below (DBUS_SERVICE_DBUS, etc) are
	 * predefined for us to just use (dbus_g_proxy_...)
	 */
	gproxy = dbus_g_proxy_new_for_name (connection,
					    DBUS_SERVICE_DBUS,
					    DBUS_PATH_DBUS,
					    DBUS_INTERFACE_DBUS);

	/* Register the service name for org.freedesktop.Tracker */
	if (!dbus_register_service (gproxy, TRACKER_DAEMON_SERVICE)) {
		return FALSE;
	}

	return TRUE;
}

gboolean
tracker_dbus_init (TrackerConfig *config)
{
	g_return_val_if_fail (TRACKER_IS_CONFIG (config), FALSE);

	/* Don't reinitialize */
	if (objects) {
		return TRUE;
	}

	/* Register names and get proxy/connection details */
	if (!dbus_register_names (config)) {
		return FALSE;
	}

	return TRUE;
}

void
tracker_dbus_shutdown (void)
{
	if (objects) {
		g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
		g_slist_free (objects);
		objects = NULL;
	}

	if (gproxy) {
		g_object_unref (gproxy);
		gproxy = NULL;
	}

	connection = NULL;
}

gboolean
tracker_dbus_register_objects (TrackerConfig	*config,
			       TrackerLanguage	*language)
{
	gpointer object, resources;
	GSList *event_sources = NULL;
	TrackerDBResultSet *result_set;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (TRACKER_IS_LANGUAGE (language), FALSE);

	if (!connection || !gproxy) {
		g_critical ("DBus support must be initialized before registering objects!");
		return FALSE;
	}

	/* Add org.freedesktop.Tracker */
	object = tracker_daemon_new (config);
	if (!object) {
		g_critical ("Could not create TrackerDaemon object to register");
		return FALSE;
	}

	dbus_register_object (connection,
			      gproxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_daemon_object_info,
			      TRACKER_DAEMON_PATH);
	objects = g_slist_prepend (objects, object);

	/* Add org.freedesktop.Tracker.Resources */
	object = tracker_resources_new ();
	if (!object) {
		g_critical ("Could not create TrackerResources object to register");
		return FALSE;
	}
	resources = object;

	dbus_register_object (connection,
			      gproxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_resources_object_info,
			      TRACKER_RESOURCES_PATH);
	objects = g_slist_prepend (objects, object);

	/* Add org.freedesktop.Tracker.Search */
	object = tracker_search_new (config, language);
	if (!object) {
		g_critical ("Could not create TrackerSearch object to register");
		return FALSE;
	}

	dbus_register_object (connection,
			      gproxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_search_object_info,
			      TRACKER_SEARCH_PATH);
	objects = g_slist_prepend (objects, object);

	/* Add org.freedesktop.Tracker.Backup */
	object = tracker_backup_new ();
	if (!object) {
		g_critical ("Could not create TrackerBackup object to register");
		return FALSE;
	}

	dbus_register_object (connection,
			      gproxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_backup_object_info,
			      TRACKER_BACKUP_PATH);
	objects = g_slist_prepend (objects, object);

	/* Reverse list since we added objects at the top each time */
	objects = g_slist_reverse (objects);

	result_set = tracker_data_query_sparql ("SELECT ?class WHERE { ?class tracker:notify true }", NULL);

	if (result_set) {
		GStrv classes_to_signal;
		guint ui, count = 0;

		classes_to_signal = tracker_dbus_query_result_to_strv (result_set, 0, &count);

		for (ui = 0; ui < count; ui++) {
			const gchar *rdf_class = classes_to_signal[ui];
			gchar *replaced;
			gchar *path, *uri, *hash;

			uri = g_strdup (rdf_class);

			hash = strrchr (uri, '#');
			if (hash == NULL) {
				/* support ontologies whose namespace uri does not end in a hash, e.g. dc */
				hash = strrchr (uri, '/');
			}
			if (hash == NULL) {
				g_critical ("Unknown namespace of property %s", uri);
			} else {
				gchar *namespace_uri = g_strndup (uri, hash - uri + 1);
				TrackerNamespace *namespace;

				namespace = tracker_ontology_get_namespace_by_uri (namespace_uri);
				if (namespace == NULL) {
					g_critical ("Unknown namespace %s of property %s", namespace_uri, uri);
				} else {
					replaced = g_strdup_printf ("%s/%s", tracker_namespace_get_prefix (namespace), hash + 1);
				}
				g_free (namespace_uri);
			}

			g_free (uri);

			path = g_strdup_printf (TRACKER_RESOURCES_CLASS_PATH,
						replaced);

			g_free (replaced);

			/* Add a org.freedesktop.Tracker.Resources.Class */
			object = tracker_resource_class_new (rdf_class);
			if (!object) {
				g_critical ("Could not create TrackerResourcesClass object to register");
				return FALSE;
			}

			dbus_register_object (connection,
					      gproxy,
					      G_OBJECT (object),
					      &dbus_glib_tracker_resources_class_object_info,
					      path);
			g_free (path);

			/* TrackerResources takes over ownership and unrefs the gobjects too */
			event_sources = g_slist_prepend (event_sources, g_object_ref (object));
			objects = g_slist_prepend (objects, object);
		}

		g_strfreev (classes_to_signal);
		g_object_unref (result_set);
	}


	tracker_resources_prepare (resources, event_sources);

	return TRUE;
}

GObject *
tracker_dbus_get_object (GType type)
{
	GSList *l;

	for (l = objects; l; l = l->next) {
		if (G_OBJECT_TYPE (l->data) == type) {
			return l->data;
		}
	}

	return NULL;
}

