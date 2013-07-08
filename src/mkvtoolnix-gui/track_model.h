#ifndef MTX_MKVTOOLNIXGUI_TRACK_MODEL_H
#define MTX_MKVTOOLNIXGUI_TRACK_MODEL_H

#include "common/common_pch.h"

#include "mkvtoolnix-gui/source_file.h"

#include <QStandardItemModel>
#include <QIcon>
#include <QList>

class TrackModel;
typedef std::shared_ptr<TrackModel> TrackModelPtr;

class TrackModel : public QStandardItemModel {
  Q_OBJECT;

protected:
  QList<Track *> *m_tracks;
  QMap<Track *, QStandardItem *> m_tracksToItems;
  QIcon m_audioIcon, m_videoIcon, m_subtitleIcon, m_attachmentIcon, m_chaptersIcon, m_tagsIcon, m_genericIcon, m_yesIcon, m_noIcon;

  debugging_option_c m_debug;

public:
  TrackModel(QObject *parent);
  virtual ~TrackModel();

  virtual void setTracks(QList<Track *> &tracks);
  virtual void addTracks(QList<TrackPtr> const &tracks);
  virtual void appendTracks(SourceFile *fileToAppendTo, QList<TrackPtr> const &tracks);

  virtual void trackUpdated(Track *track);

protected:
  QList<QStandardItem *>createRow(Track *track);

public:                         // static
  static Track *fromIndex(QModelIndex const &index);
  static int rowForTrack(QList<Track *> const &tracks, Track *trackToLookFor);
};

Q_DECLARE_METATYPE(Track *)

#endif  // MTX_MKVTOOLNIXGUI_TRACK_MODEL_H
