/*
 * Copyright (C) 2010 Igalia S.L.
 *
 * Authors: Juan A. Suarez Romero <jasuarez@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib.h>

#include "media-server2-private.h"
#include "media-server2.h"

#include "media-server2-glue.h"

#define ENTRY_POINT_IFACE "/org/gnome/UPnP/MediaServer2"
#define ENTRY_POINT_NAME  "org.gnome.UPnP.MediaServer2"

#define ID_PREFIX_AUDIO     "gra://"
#define ID_PREFIX_CONTAINER "grc://"
#define ID_PREFIX_IMAGE     "gri://"
#define ID_PREFIX_VIDEO     "grv://"
#define ID_ROOT             "0"
#define ID_SEPARATOR        "/"

#define MS_INT_VALUE_UNKNOWN -1
#define MS_STR_VALUE_UNKNOWN ""

#define MS_TYPE_AUDIO     "audio"
#define MS_TYPE_CONTAINER "container"
#define MS_TYPE_IMAGE     "image"
#define MS_TYPE_VIDEO     "video"

#define MS_PROP_ALBUM        "album"
#define MS_PROP_ARTIST       "artist"
#define MS_PROP_BITRATE      "bitrate"
#define MS_PROP_CHILD_COUNT  "child-count"
#define MS_PROP_DISPLAY_NAME "display-name"
#define MS_PROP_DURATION     "duration"
#define MS_PROP_GENRE        "genre"
#define MS_PROP_HEIGHT       "height"
#define MS_PROP_MIME_TYPE    "mime-type"
#define MS_PROP_PARENT       "parent"
#define MS_PROP_TYPE         "type"
#define MS_PROP_URLS         "URLs"
#define MS_PROP_WIDTH        "width"

#define DBUS_TYPE_G_ARRAY_OF_STRING                             \
  (dbus_g_type_get_collection ("GPtrArray", G_TYPE_STRING))

#define MEDIA_SERVER2_GET_PRIVATE(o)                                    \
  G_TYPE_INSTANCE_GET_PRIVATE((o), MEDIA_SERVER2_TYPE, MediaServer2Private)

/*
 * Private MediaServer2 structure
 *   data: holds stuff for owner
 *   get_children: function to get children
 *   get_properties: function to get properties
 */
struct _MediaServer2Private {
  gpointer *data;
  GetChildrenFunc get_children;
  GetPropertiesFunc get_properties;
};

G_DEFINE_TYPE (MediaServer2, media_server2, G_TYPE_OBJECT);

/* Registers the MediaServer2 object in dbus */
static gboolean
media_server2_dbus_register (MediaServer2 *server,
                             const gchar *name)
{
  DBusGConnection *connection;
  DBusGProxy *gproxy;
  GError *error = NULL;
  gchar *dbus_name;
  gchar *dbus_path;
  guint request_name_result;

  connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (!connection) {
    g_printerr ("Could not connect to session bus, %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  gproxy = dbus_g_proxy_new_for_name (connection,
                                      DBUS_SERVICE_DBUS,
                                      DBUS_PATH_DBUS,
                                      DBUS_INTERFACE_DBUS);

  /* Request name */
  dbus_name = g_strconcat (ENTRY_POINT_NAME, ".", name, NULL);
  if (!org_freedesktop_DBus_request_name (gproxy,
                                          dbus_name,
                                          DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                          &request_name_result,
                                          NULL))  {
      return FALSE;
  }
  g_free (dbus_name);
  g_object_unref (gproxy);

  /* Register object */
  dbus_path = g_strconcat (ENTRY_POINT_IFACE, "/", name, NULL);
  dbus_g_connection_register_g_object (connection,
                                       dbus_path,
                                       G_OBJECT (server));
  g_free (dbus_path);

  return TRUE;
}

/* Class init function */
static void
media_server2_class_init (MediaServer2Class *klass)
{
  g_type_class_add_private (klass, sizeof (MediaServer2Private));

  /* Register introspection */
  dbus_g_object_type_install_info (MEDIA_SERVER2_TYPE,
                                   &dbus_glib_media_server2_object_info);
}

/* Object init function */
static void
media_server2_init (MediaServer2 *server)
{
  server->priv = MEDIA_SERVER2_GET_PRIVATE (server);
}

/* Free gvalue */
static void
free_value (GValue *value)
{
  g_value_unset (value);
  g_free (value);
}

static void
free_ptr_array (GPtrArray *array)
{
  g_ptr_array_free (array, TRUE);
}

/* Puts a string in a gvalue */
static GValue *
str_to_value (const gchar *str)
{
  GValue *val = NULL;

  if (str) {
    val = g_new0 (GValue, 1);
    g_value_init (val, G_TYPE_STRING);
    g_value_set_string (val, str);
  }

  return val;
}

/* Puts an int in a gvalue */
static GValue *
int_to_value (gint number)
{
  GValue *val = NULL;

  val = g_new0 (GValue, 1);
  g_value_init (val, G_TYPE_INT);
  g_value_set_int (val, number);

  return val;
}

/* Puts a gptrarray in a gvalue */
static GValue *
ptrarray_to_value (GPtrArray *array)
{
  GValue *val = NULL;

  val = g_new0 (GValue, 1);
  g_value_init (val, DBUS_TYPE_G_ARRAY_OF_STRING);
  g_value_take_boxed (val, array);

  return val;
}

/* Get unknown value for the property */
static GValue *
get_unknown_value (const gchar *property)
{
  GValue *val;
  GPtrArray *ptrarray;

  if (g_strcmp0 (property, MS2_PROP_ID) == 0 ||
      g_strcmp0 (property, MS2_PROP_PARENT) == 0 ||
      g_strcmp0 (property, MS2_PROP_DISPLAY_NAME) == 0 ||
      g_strcmp0 (property, MS2_PROP_TYPE) == 0 ||
      g_strcmp0 (property, MS2_PROP_ICON) == 0 ||
      g_strcmp0 (property, MS2_PROP_MIME_TYPE) == 0 ||
      g_strcmp0 (property, MS2_PROP_ARTIST) == 0 ||
      g_strcmp0 (property, MS2_PROP_ALBUM) == 0 ||
      g_strcmp0 (property, MS2_PROP_DATE) == 0 ||
      g_strcmp0 (property, MS2_PROP_DLNA_PROFILE) == 0 ||
      g_strcmp0 (property, MS2_PROP_THUMBNAIL) == 0 ||
      g_strcmp0 (property, MS2_PROP_GENRE) == 0) {
    val = str_to_value (MS2_UNKNOWN_STR);
  } else if (g_strcmp0 (property, MS2_PROP_URLS) == 0) {
    ptrarray = g_ptr_array_sized_new (1);
    g_ptr_array_add (ptrarray, g_strdup (MS2_UNKNOWN_STR));
    val = ptrarray_to_value (ptrarray);
  } else {
    val = int_to_value (MS2_UNKNOWN_INT);
  }

  return val;
}

/* Returns an array of properties values suitable to send as dbus reply */
static GPtrArray *
get_array_properties (const gchar *id,
                      const gchar **filter,
                      GHashTable *properties)
{
  GPtrArray *prop_array;
  gint i;
  GValue *val;

  prop_array = g_ptr_array_sized_new (g_strv_length ((gchar **) filter));
  for (i = 0; filter[i]; i++) {
    if (properties) {
      val = g_hash_table_lookup (properties, filter[i]);
      if (val) {
        g_ptr_array_add (prop_array, val);
      } else {
        val = get_unknown_value (filter[i]);
        g_hash_table_insert (properties, g_strdup (filter[i]), val);
        g_ptr_array_add (prop_array, val);
      }
    } else {
      val = get_unknown_value (filter[i]);
      g_ptr_array_add (prop_array, val);
    }
  }

  return prop_array;
}

/* Return a hashtable with children and properties suitable to send as dbus
   reply */
static GHashTable *
get_hash_children (GList *children,
                   const gchar **filter)
{
  GHashTable *children_hash;
  GList *child;
  GPtrArray *prop_array;
  GValue *val_id;
  gchar *id;;

  children_hash = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         (GDestroyNotify) g_free,
                                         (GDestroyNotify) free_ptr_array);

  for (child = children; child; child = g_list_next (child)) {
    val_id = g_hash_table_lookup (child->data, MS2_PROP_ID);
    if (val_id && G_VALUE_HOLDS_STRING (val_id)) {
      id = g_value_dup_string (val_id);
    }

    if (id) {
      prop_array = get_array_properties (id, filter, child->data);
      g_hash_table_insert (children_hash, id, prop_array);
    }
  }

  return children_hash;
}

/**
 * media_server2_get_properties:
 * @server: 
 * @id: 
 * @filter: 
 * @context: 
 * @error: 
 *
 * 
 *
 * Returns: 
 **/
gboolean
media_server2_get_properties (MediaServer2 *server,
                              const gchar *id,
                              const gchar **filter,
                              DBusGMethodInvocation *context,
                              GError **error)
{
  GError *prop_error = NULL;
  GHashTable *properties = NULL;
  GPtrArray *prop_array = NULL;

  if (server->priv->get_properties) {
    properties = server->priv->get_properties (id,
                                               filter,
                                               server->priv->data,
                                               &prop_error);

    if (prop_error) {
      if (error) {
        *error = g_error_new_literal (MEDIA_SERVER2_ERROR,
                                      MEDIA_SERVER2_ERROR_GENERAL,
                                      prop_error->message);
        dbus_g_method_return_error (context, *error);
      }

      g_error_free (prop_error);

      return FALSE;
    }
  }

  prop_array = get_array_properties (id, filter, properties);
  dbus_g_method_return (context, prop_array);

  /* Free content */
  if (properties) {
    g_hash_table_unref (properties);
  } else {
    g_ptr_array_foreach (prop_array, (GFunc) free_value, NULL);
  }
  g_ptr_array_free (prop_array, TRUE);

  return TRUE;
}

/**
 * media_server2_get_children:
 * @server: 
 * @id: 
 * @offset: 
 * @max_count: 
 * @filter: 
 * @context: 
 * @error: 
 *
 * 
 *
 * Returns: 
 **/
gboolean
media_server2_get_children (MediaServer2 *server,
                            const gchar *id,
                            guint offset,
                            gint max_count,
                            const gchar **filter,
                            DBusGMethodInvocation *context,
                            GError **error)

{
  GError *child_error = NULL;
  GHashTable *children_hash = NULL;
  GList *children = NULL;

  if (server->priv->get_children) {
    children = server->priv->get_children (id,
                                           offset,
                                           max_count < 0? G_MAXINT: max_count,
                                           filter,
                                           server->priv->data,
                                           &child_error);

    if (child_error) {
      if (error) {
        *error = g_error_new_literal (MEDIA_SERVER2_ERROR,
                                      MEDIA_SERVER2_ERROR_GENERAL,
                                      child_error->message);
        dbus_g_method_return_error (context, *error);
      }

      g_error_free (child_error);

      return FALSE;
    }
  }

  children_hash = get_hash_children (children, filter);
  dbus_g_method_return (context, children_hash);

  /* Free content */
  g_hash_table_unref (children_hash);
  g_list_foreach (children, (GFunc) g_hash_table_unref, NULL);
  g_list_free (children);

  return TRUE;
}

/*********** PUBLIC API ***********/

/**
 * media_server2_new:
 * @name: 
 * @data: 
 *
 * 
 *
 * Returns: 
 **/
MediaServer2 *
media_server2_new (const gchar *name,
                   gpointer data)
{
  MediaServer2 *server;

  server = g_object_new (MEDIA_SERVER2_TYPE, NULL);

  server->priv->data = data;

  if (!media_server2_dbus_register (server, name)) {
    g_object_unref (server);
    return NULL;
  } else {
    return server;
  }
}

/**
 * media_server2_set_get_properties_func:
 * @server: 
 * @f: 
 *
 * 
 **/
void
media_server2_set_get_properties_func (MediaServer2 *server,
                                       GetPropertiesFunc get_properties_func)
{
  g_return_if_fail (IS_MEDIA_SERVER2 (server));

  server->priv->get_properties = get_properties_func;
}

/**
 * media_server2_set_get_children_func:
 * @server: 
 * @f: 
 *
 * 
 **/
void
media_server2_set_get_children_func (MediaServer2 *server,
                                     GetChildrenFunc get_children_func)
{
  g_return_if_fail (IS_MEDIA_SERVER2 (server));

  server->priv->get_children = get_children_func;
}

/**
 * media_server2_new_properties_hashtable:
 * @id: 
 *
 * 
 *
 * Returns: 
 **/
GHashTable *
media_server2_new_properties_hashtable (const gchar *id)
{
  GHashTable *properties;

  properties = g_hash_table_new_full (g_str_hash,
                                      g_str_equal,
                                      NULL,
                                      (GDestroyNotify) free_value);
  if (id) {
    g_hash_table_insert (properties, MS2_PROP_ID, str_to_value (id));
  }

  return properties;
}

/**
 * media_server2_set_parent:
 * @properties: 
 * @parent: 
 *
 * 
 **/
void
media_server2_set_parent (GHashTable *properties,
                          const gchar *parent)
{
  g_return_if_fail (properties);

  if (parent) {
    g_hash_table_insert (properties,
                         MS2_PROP_PARENT,
                         str_to_value (parent));
  }
}

/**
 * media_server2_set_display_name:
 * @properties: 
 * @display_name: 
 *
 * 
 **/
void
media_server2_set_display_name (GHashTable *properties,
                                const gchar *display_name)
{
  g_return_if_fail (properties);

  if (display_name) {
    g_hash_table_insert (properties,
                         MS2_PROP_DISPLAY_NAME,
                         str_to_value (display_name));
  }
}

/**
 * media_server2_set_type_container:
 * @properties: 
 *
 * 
 **/
void
media_server2_set_type_container (GHashTable *properties)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_TYPE,
                       str_to_value (MS2_TYPE_CONTAINER));
}

/**
 * media_server2_set_type_video:
 * @properties: 
 *
 * 
 **/
void
media_server2_set_type_video (GHashTable *properties)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_TYPE,
                       str_to_value (MS2_TYPE_VIDEO));
}

/**
 * media_server2_set_type_movie:
 * @properties: 
 *
 * 
 **/
void
media_server2_set_type_movie (GHashTable *properties)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_TYPE,
                       str_to_value (MS2_TYPE_MOVIE));
}

/**
 * media_server2_set_type_audio:
 * @properties: 
 *
 * 
 **/
void
media_server2_set_type_audio (GHashTable *properties)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_TYPE,
                       str_to_value (MS2_TYPE_AUDIO));
}

/**
 * media_server2_set_type_music:
 * @properties: 
 *
 * 
 **/
void
media_server2_set_type_music (GHashTable *properties)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_TYPE,
                       str_to_value (MS2_TYPE_MUSIC));
}

/**
 * media_server2_set_type_image:
 * @properties: 
 *
 * 
 **/
void
media_server2_set_type_image (GHashTable *properties)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_TYPE,
                       str_to_value (MS2_TYPE_IMAGE));
}

/**
 * media_server2_set_type_photo:
 * @properties: 
 *
 * 
 **/
void
media_server2_set_type_photo (GHashTable *properties)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_TYPE,
                       str_to_value (MS2_TYPE_PHOTO));
}

/**
 * media_server2_set_icon:
 * @properties: 
 * @icon: 
 *
 * 
 **/
void
media_server2_set_icon (GHashTable *properties,
                        const gchar *icon)
{
  g_return_if_fail (properties);

  if (icon) {
    g_hash_table_insert (properties,
                         MS2_PROP_ICON,
                         str_to_value (icon));
  }
}

/**
 * media_server2_set_mime_type:
 * @properties: 
 * @mime_type: 
 *
 * 
 **/
void
media_server2_set_mime_type (GHashTable *properties,
                             const gchar *mime_type)
{
  g_return_if_fail (properties);

  if (mime_type) {
    g_hash_table_insert (properties,
                         MS2_PROP_MIME_TYPE,
                         str_to_value (mime_type));
  }
}

/**
 * media_server2_set_artist:
 * @properties: 
 * @artist: 
 *
 * 
 **/
void
media_server2_set_artist (GHashTable *properties,
                          const gchar *artist)
{
  g_return_if_fail (properties);

  if (artist) {
    g_hash_table_insert (properties,
                         MS2_PROP_ARTIST,
                         str_to_value (artist));
  }
}

/**
 * media_server2_set_album:
 * @properties: 
 * @album: 
 *
 * 
 **/
void
media_server2_set_album (GHashTable *properties,
                         const gchar *album)
{
  g_return_if_fail (properties);

  if (album) {
    g_hash_table_insert (properties,
                         MS2_PROP_ALBUM,
                         str_to_value (album));
  }
}

/**
 * media_server2_set_date:
 * @properties: 
 * @date: 
 *
 * 
 **/
void
media_server2_set_date (GHashTable *properties,
                        const gchar *date)
{
  g_return_if_fail (properties);

  if (date) {
    g_hash_table_insert (properties,
                         MS2_PROP_ALBUM,
                         str_to_value (date));
  }
}

/**
 * media_server2_set_dlna_profile:
 * @properties: 
 * @dlna_profile: 
 *
 * 
 **/
void
media_server2_set_dlna_profile (GHashTable *properties,
                                const gchar *dlna_profile)
{
  g_return_if_fail (properties);

  if (dlna_profile) {
    g_hash_table_insert (properties,
                         MS2_PROP_DLNA_PROFILE,
                         str_to_value (dlna_profile));
  }
}

/**
 * media_server2_set_thumbnail:
 * @properties: 
 * @thumbnail: 
 *
 * 
 **/
void
media_server2_set_thumbnail (GHashTable *properties,
                             const gchar *thumbnail)
{
  g_return_if_fail (properties);

  if (thumbnail) {
    g_hash_table_insert (properties,
                         MS2_PROP_THUMBNAIL,
                         str_to_value (thumbnail));
  }
}

/**
 * media_server2_set_genre:
 * @properties: 
 * @genre: 
 *
 * 
 **/
void
media_server2_set_genre (GHashTable *properties,
                         const gchar *genre)
{
  g_return_if_fail (properties);

  if (genre) {
    g_hash_table_insert (properties,
                         MS2_PROP_GENRE,
                         str_to_value (genre));
  }
}

/**
 * media_server2_set_child_count:
 * @properties: 
 * @child_count: 
 *
 * 
 **/
void
media_server2_set_child_count (GHashTable *properties,
                               gint child_count)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_CHILD_COUNT,
                       int_to_value (child_count));
}

/**
 * media_server2_set_size:
 * @properties: 
 * @size: 
 *
 * 
 **/
void
media_server2_set_size (GHashTable *properties,
                        gint size)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_SIZE,
                       int_to_value (size));
}

/**
 * media_server2_set_duration:
 * @properties: 
 * @duration: 
 *
 * 
 **/
void
media_server2_set_duration (GHashTable *properties,
                            gint duration)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_DURATION,
                       int_to_value (duration));
}

/**
 * media_server2_set_bitrate:
 * @properties: 
 * @bitrate: 
 *
 * 
 **/
void
media_server2_set_bitrate (GHashTable *properties,
                           gint bitrate)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_BITRATE,
                       int_to_value (bitrate));
}

/**
 * media_server2_set_sample_rate:
 * @properties: 
 * @sample_rate: 
 *
 * 
 **/
void
media_server2_set_sample_rate (GHashTable *properties,
                               gint sample_rate)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_SAMPLE_RATE,
                       int_to_value (sample_rate));
}

/**
 * media_server2_set_bits_per_sample:
 * @properties: 
 * @bits_per_sample: 
 *
 * 
 **/
void
media_server2_set_bits_per_sample (GHashTable *properties,
                                   gint bits_per_sample)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_BITS_PER_SAMPLE,
                       int_to_value (bits_per_sample));
}

/**
 * media_server2_set_width:
 * @properties: 
 * @width: 
 *
 * 
 **/
void
media_server2_set_width (GHashTable *properties,
                         gint width)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_WIDTH,
                       int_to_value (width));
}

/**
 * media_server2_set_height:
 * @properties: 
 * @height: 
 *
 * 
 **/
void
media_server2_set_height (GHashTable *properties,
                          gint height)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_HEIGHT,
                       int_to_value (height));
}

/**
 * media_server2_set_pixel_width:
 * @properties: 
 * @pixel_width: 
 *
 * 
 **/
void
media_server2_set_pixel_width (GHashTable *properties,
                               gint pixel_width)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_PIXEL_WIDTH,
                       int_to_value (pixel_width));
}

/**
 * media_server2_set_pixel_height:
 * @properties: 
 * @pixel_height: 
 *
 * 
 **/
void
media_server2_set_pixel_height (GHashTable *properties,
                                gint pixel_height)
{
  g_return_if_fail (properties);

  g_hash_table_insert (properties,
                       MS2_PROP_PIXEL_HEIGHT,
                       int_to_value (pixel_height));
}

/**
 * media_server2_set_urls:
 * @properties: 
 * @urls: 
 *
 * 
 **/
void
media_server2_set_urls (GHashTable *properties,
                        gchar **urls)
{
  GPtrArray *url_array;
  gint i;

  g_return_if_fail (properties);

  if (urls && urls[0]) {
    url_array = g_ptr_array_sized_new (g_strv_length (urls));
    for (i = 0; urls[i]; i++) {
      g_ptr_array_add (url_array, g_strdup (urls[i]));
    }

    g_hash_table_insert (properties,
                         MS2_PROP_URLS,
                         ptrarray_to_value (url_array));
  }
}