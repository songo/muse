//=========================================================
//  MusE
//  Linux Music Editor
//  $Id: routedialog.cpp,v 1.5.2.2 2007/01/04 00:35:17 terminator356 Exp $
//
//  (C) Copyright 2004 Werner Schweer (ws@seh.de)
//  (C) Copyright 2015 Tim E. Real (terminator356 on sourceforge)
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; version 2 of
//  the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
//=========================================================

#include <QCloseEvent>
#include <QScrollBar>
#include <QVector>
#include <QList>
#include <QPainter>
#include <QPalette>
#include <Qt>
#include <QMouseEvent>
#include <QRect>
#include <QPoint>
#include <QModelIndex>
#include <QString>
#include <QHeaderView>
#include <QLayout>
#include <QFlags>
#include <QVariant>

#include "routedialog.h"
#include "globaldefs.h"
#include "gconfig.h"
#include "track.h"
#include "song.h"
#include "audio.h"
#include "driver/jackaudio.h"
#include "globaldefs.h"
#include "app.h"
#include "operations.h"
#include "icons.h"

// For debugging output: Uncomment the fprintf section.
#define DEBUG_PRST_ROUTES(dev, format, args...) // fprintf(dev, format, ##args);

// Undefine if and when multiple output routes are added to midi tracks.
#define _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_

// An arbitrarily large value for size hint calculations.
#define _VERY_LARGE_INTEGER_ 1000000

namespace MusEGui {

const QString RouteDialog::tracksCat(QObject::tr("Tracks:"));
const QString RouteDialog::midiPortsCat(QObject::tr("Midi ports:"));
const QString RouteDialog::midiDevicesCat(QObject::tr("Midi devices:"));
const QString RouteDialog::jackCat(QObject::tr("Jack:"));
const QString RouteDialog::jackMidiCat(QObject::tr("Jack midi:"));

const int RouteDialog::channelDotDiameter = 12;
const int RouteDialog::channelDotSpacing = 1;
const int RouteDialog::channelDotsPerGroup = 4;
const int RouteDialog::channelDotGroupSpacing = 3;
const int RouteDialog::channelDotsMargin = 1;
const int RouteDialog::channelBarHeight = RouteDialog::channelDotDiameter + 2 * RouteDialog::channelDotsMargin;
const int RouteDialog::channelLineWidth = 1;
const int RouteDialog::channelLinesSpacing = 1;
const int RouteDialog::channelLinesMargin = 1;

std::list<QString> tmpJackInPorts;
std::list<QString> tmpJackOutPorts;
std::list<QString> tmpJackMidiInPorts;
std::list<QString> tmpJackMidiOutPorts;

//---------------------------------------------------------
//   RouteChannelsList
//---------------------------------------------------------

int RouteChannelsList::connectedChannels() const
{
  int n = 0;
  const int sz = size();
  for(int i = 0; i < sz; ++i)
    if(at(i)._connected)
      ++n;
  return n;
}

// Static.
int RouteChannelsList::channelsPerWidth(int width)
{
//   if(width <= 0)
//     return size();
  if(width < 0)
    width = _VERY_LARGE_INTEGER_;
  
  int groups_per_col = (width - 2 * RouteDialog::channelDotsMargin) / 
                        (RouteDialog::channelDotGroupSpacing + 
                         RouteDialog::channelDotsPerGroup * (RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing));
  if(groups_per_col < 1)
    groups_per_col = 1;
  return RouteDialog::channelDotsPerGroup * groups_per_col;
}

// Static.
int RouteChannelsList::groupsPerChannels(int channels)
{
  
  int groups = channels / RouteDialog::channelDotsPerGroup;
  //if(groups < 1)
  //  groups = 1;
  if(channels % RouteDialog::channelDotsPerGroup)
    ++groups;
  return groups;
}

int RouteChannelsList::barsPerColChannels(int cc) const
{
  if(cc == 0)
    return 0;
  const int chans = size();
  int bars = chans / cc;
  if(chans % cc)
    ++bars;
  //if(chan_rows < 1)
  //  chan_rows = 1;
  return bars;
}

// Static.
int RouteChannelsList::minimumWidthHint()
{
  return RouteDialog::channelDotsPerGroup * (RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing) +
         RouteDialog::channelDotGroupSpacing +
         2 * RouteDialog::channelDotsMargin;
}

int RouteChannelsList::widthHint(int width) const
{
  const int chans = size();
  int chans_per_col = channelsPerWidth(width);
  // Limit to actual number of channels available.
  if(chans_per_col > chans)
    chans_per_col = chans;
  const int groups_per_col = groupsPerChannels(chans_per_col);
  return chans_per_col * (RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing) +
         groups_per_col * RouteDialog::channelDotGroupSpacing +
         2 * RouteDialog::channelDotsMargin;
}

int RouteChannelsList::heightHint(int width) const
{
  const int chans = size();
  int chans_per_col = channelsPerWidth(width);
  // Limit to actual number of channels available.
  if(chans_per_col > chans)
    chans_per_col = chans;
  const int bars = barsPerColChannels(chans_per_col);
  return bars * RouteDialog::channelBarHeight + 
         connectedChannels() * (RouteDialog::channelLinesSpacing + RouteDialog::channelLineWidth) + 
         4 * RouteDialog::channelLinesMargin;
}

//---------------------------------------------------------
//   RouteTreeWidgetItem
//---------------------------------------------------------

void RouteTreeWidgetItem::init()
{
  _curChannel = 0;
  setChannels();
  //computeChannelYValues();
  
  // A data role to pass the item type from item to delegate.
  //setData(RouteDialog::ROUTE_NAME_COL, TypeRole, QVariant::fromValue<int>(type()));
}

bool RouteTreeWidgetItem::setChannels()
{
  bool changed = false;
  
  switch(type())
  {
    case NormalItem:
    case CategoryItem:
    case RouteItem:
    break;
    
    case ChannelsItem:
      switch(_route.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          if(_route.track)
          {
            MusECore::RouteCapabilitiesStruct rcaps = _route.track->routeCapabilities();
            int chans = 0;
            switch(_route.track->type())
            {
              case MusECore::Track::AUDIO_INPUT:
                chans = _isInput ? rcaps._trackChannels._outChannels : rcaps._jackChannels._inChannels;
              break;
              case MusECore::Track::AUDIO_OUTPUT:
                chans = _isInput ? rcaps._jackChannels._outChannels : rcaps._trackChannels._inChannels;
              break;
              case MusECore::Track::MIDI:
              case MusECore::Track::DRUM:
              case MusECore::Track::NEW_DRUM:
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
                chans = _isInput ? MIDI_CHANNELS : rcaps._midiPortChannels._inChannels;
#else                  
                chans = _isInput ? rcaps._midiPortChannels._outChannels : rcaps._midiPortChannels._inChannels;
#endif              
              break;
                
              case MusECore::Track::WAVE:
              case MusECore::Track::AUDIO_AUX:
              case MusECore::Track::AUDIO_SOFTSYNTH:
              case MusECore::Track::AUDIO_GROUP:
                chans = _isInput ? rcaps._trackChannels._outChannels : rcaps._trackChannels._inChannels;
              break;
            }
            
            if(chans != _channels.size())
            {
              _channels.resize(chans);
              changed = true;
            }
          }
        break;  

        case MusECore::Route::JACK_ROUTE:  
        case MusECore::Route::MIDI_DEVICE_ROUTE:  
        case MusECore::Route::MIDI_PORT_ROUTE:
        break;  
      }
    break;  
  }
  
  
  if(changed)
  {
    _curChannel = 0;
    //computeChannelYValues();
  }

  return changed;
}

void RouteTreeWidgetItem::getSelectedRoutes(MusECore::RouteList& routes)
{
  switch(type())
  {
    case NormalItem:
    case CategoryItem:
    break;  
    case RouteItem:
      if(isSelected())
        routes.push_back(_route);
    break;  
    case ChannelsItem:
      switch(_route.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          if(_route.track)
          {
            MusECore::Route r(_route);
            const int sz = _channels.size();
            if(_route.track->isMidiTrack())
            {  
              for(int i = 0; i < sz && i < MIDI_CHANNELS; ++i)
              {
                //if(_channels.testBit(i))
                if(_channels.selected(i))
                {
                  //r.channel = (1 << i);
                  r.channel = i;
                  routes.push_back(r);
                }
              }
            }
            else
            {
              for(int i = 0; i < sz; ++i)
              {
                //if(_channels.testBit(i))
                if(_channels.selected(i))
                {
                  r.channel = i;
                  routes.push_back(r);
                }
              }
            }
          }
        break;
        case MusECore::Route::JACK_ROUTE:
        case MusECore::Route::MIDI_DEVICE_ROUTE:
        case MusECore::Route::MIDI_PORT_ROUTE:
          if(isSelected())
            routes.push_back(_route);
        break;
      }
      
    break;  
  }
}

int RouteTreeWidgetItem::channelAt(const QPoint& pt, const QRect& rect) const
{
//   if(!treeWidget()->viewport())
//     return false;
  
  RouteTreeWidget* rtw = qobject_cast<RouteTreeWidget*>(treeWidget());
  if(!rtw)
    return false;
  
  const int col = rtw->columnAt(pt.x());
  const int col_width = rtw->columnWidth(col); 
  //const int view_width = rtw->viewport()->width();
  const int chans = _channels.size();
  const int view_offset = rtw->header()->offset();
//   const int x_offset = (_isInput ? view_width - _channels.widthHint(view_width) - view_offset : -view_offset);
  const int x_offset = (_isInput ? 
                        //view_width - _channels.widthHint(rtw->wordWrap() ? view_width : -1) - view_offset : -view_offset);
                        col_width - _channels.widthHint(rtw->channelWrap() ? col_width : -1) - view_offset : -view_offset);

  QPoint p(pt.x() - x_offset, pt.y() - rect.y());
  
  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::channelAt() pt x:%d y:%d rect x:%d y:%d w:%d h:%d view_offset:%d x_offset:%d col w:%d header w:%d view w:%d p x:%d y:%d\n", 
          pt.x(), pt.y(), rect.x(), rect.y(), rect.width(), rect.height(), view_offset, x_offset, 
          rtw->columnWidth(col), rtw->header()->sectionSize(col), view_width, p.x(), p.y());  // REMOVE Tim.
  
  for(int i = 0; i < chans; ++i)
  {
    const RouteChannelsStruct& ch_struct = _channels.at(i);
    const QRect& ch_rect = ch_struct._buttonRect;
    if(ch_rect.contains(p))
      return i;
  }
  return -1;
}  
/*  
  const int channels = _channels.size();
  //const QRect rect = visualItemRect(item);
  QPoint p = pt - rect.topLeft();

  int w = RouteDialog::channelDotsMargin * 2 + RouteDialog::channelDotDiameter * channels;
  if(channels > 1)
    w += RouteDialog::channelDotSpacing * (channels - 1);
  if(channels > 4)
    w += RouteDialog::channelDotGroupSpacing * (channels - 1) / 4;
  
  const int xoff =_isInput ? rect.width() - w : RouteDialog::channelDotsMargin;
  const int yoff = RouteDialog::channelDotsMargin + (_isInput ? channels : 0);
  p.setY(p.y() - yoff);
  p.setX(p.x() - xoff);
  if(p.y() < 0 || p.y() >= RouteDialog::channelDotDiameter)
    return -1;
  for(int i = 0; i < channels; ++i)
  {
    if(p.x() < 0)
      return -1;
    if(p.x() < RouteDialog::channelDotDiameter)
      return i;
    p.setX(p.x() - RouteDialog::channelDotDiameter - RouteDialog::channelDotSpacing);
    if(i && ((i % 4) == 0))
      p.setX(p.x() - RouteDialog::channelDotGroupSpacing);
  }
  return -1;
}*/

// int RouteTreeWidgetItem::connectedChannels() const
// {
//   int n = 0;
//   //const int sz = _channelYValues.size();
//   const int sz = _channels.size();
//   for(int i = 0; i < sz; ++i)
//     //if(_channelYValues.at(i) != -1)
//     if(_channels.at(i)._connected)
//       ++n;
//   return n;
// }

// int RouteTreeWidgetItem::channelsPerWidth(int w) const
// {
//   if(type() == ChannelsItem) 
//   {
//     if(w == -1)
// //       w = treeWidget()->columnWidth(RouteDialog::ROUTE_NAME_COL);
//       w = treeWidget()->viewport()->width();
//     int groups_per_col = (w - 2 * RouteDialog::channelDotsMargin) / 
//                          (RouteDialog::channelDotGroupSpacing + RouteDialog::channelDotsPerGroup * (RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing));
//     if(groups_per_col < 1)
//       groups_per_col = 1;
//     
//     return RouteDialog::channelDotsPerGroup * groups_per_col;
//   }
//   return 0;
// }

// int RouteTreeWidgetItem::groupsPerChannels(int c) const
// {
//   
//   int groups = c / RouteDialog::channelDotsPerGroup;
//   //if(groups < 1)
//   //  groups = 1;
//   if(c % RouteDialog::channelDotsPerGroup)
//     ++groups;
//   return groups;
// }

// int RouteTreeWidgetItem::barsPerColChannels(int cc) const
// {
//   if(cc == 0)
//     return 0;
//   const int chans = _channels.size();
//   int bars = chans / cc;
//   if(chans % cc)
//     ++bars;
//   //if(chan_rows < 1)
//   //  chan_rows = 1;
//   return bars;
// }


void RouteTreeWidgetItem::computeChannelYValues(int col_width)
{
  //_channelYValues.resize();
  if(type() != ChannelsItem)
    return;
  //_channelYValues.fill(-1);
  _channels.fillConnected(false);
  switch(_route.type)
  {
    case MusECore::Route::TRACK_ROUTE:
      if(_route.track)
      {
        //_channelYValues.fill(-1);
        //_channels.fillConnected(false);
        
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
        if(_isInput && _route.track->isMidiTrack())
          _channels.setConnected(static_cast<MusECore::MidiTrack*>(_route.track)->outChannel(), true);
        else
#endif          
        {
          const MusECore::RouteList* rl = _isInput ? _route.track->outRoutes() : _route.track->inRoutes();
          for(MusECore::ciRoute ir = rl->begin(); ir != rl->end(); ++ir)
          {
            switch(ir->type)
            {
              case MusECore::Route::TRACK_ROUTE:
                //if(ir->track && ir->channel != -1)
                if(ir->channel != -1)
                {
                  //if(ir->channel >= _channelYValues.size())
                  //if(ir->channel >= _channels.size())
                  //{
                    //DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::computeChannelYValues() Error: iRoute channel:%d out of channels range:%d\n", ir->channel, _channelYValues.size());
                  //  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::computeChannelYValues() Error: iRoute channel:%d out of channels range:%d\n", ir->channel, _channels.size());
                  //  break; 
                  //}
                  // Mark the channel as used with a zero, for now.
                  //_channelYValues.replace(ir->channel, 0);
                  _channels.setConnected(ir->channel, true);
                }
              break;

              case MusECore::Route::MIDI_PORT_ROUTE:
                if(ir->isValid() && ir->channel != -1)
                {
  //                 for(int i = 0; i < MIDI_CHANNELS; ++i)
  //                 {
  //                   if(ir->channel & (1 << i))
  //                   {
  //                     // Mark the channel as used with a zero, for now.
  //                     //_channelYValues.replace(i, 0);
  //                     _channels.setConnected(i, true);
  //                   }
  //                 }
                  _channels.setConnected(ir->channel, true);
                }
              break;

              case MusECore::Route::JACK_ROUTE:
                if(ir->channel != -1)
                  _channels.setConnected(ir->channel, true);
              break;

              case MusECore::Route::MIDI_DEVICE_ROUTE:
              break;
            }
          }
        }
      }
    break;

    case MusECore::Route::JACK_ROUTE:
    case MusECore::Route::MIDI_DEVICE_ROUTE:
    case MusECore::Route::MIDI_PORT_ROUTE:
    break;
  }

  const int chans = _channels.size();
//   int w = RouteDialog::channelDotsMargin * 2 + RouteDialog::channelDotDiameter * chans;
//   //int w = RouteDialog::channelDotsMargin * 2 + (RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing) * chans;
//   if(chans > 1)
//     w += RouteDialog::channelDotSpacing * (chans - 1);
//   if(chans > RouteDialog::channelDotsPerGroup)
//     w += RouteDialog::channelDotGroupSpacing * (chans - 1) / RouteDialog::channelDotsPerGroup;

  //const int col_width = treeWidget()->columnWidth(RouteDialog::ROUTE_NAME_COL);
//   if(col_width == -1)
//     col_width = treeWidget()->columnWidth(RouteDialog::ROUTE_NAME_COL);
//     col_width = treeWidget()->viewport()->width();
  int chans_per_w = _channels.channelsPerWidth(col_width);
  // Limit to actual number of channels available.
  if(chans_per_w > chans)
    chans_per_w = chans;

  //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint src list width:%d src viewport width:%d\n", router->newSrcList->width(), router->newSrcList->viewport()->width());  // REMOVE Tim.
  //int x = _isInput ? router->newSrcList->viewport()->width() - w : RouteDialog::midiDotsMargin;
  //int x = _isInput ? painter->device()->width() - w : RouteDialog::channelDotsMargin;
  //const int x_orig = _isInput ? treeWidget()->width() - w : RouteDialog::channelDotsMargin;
  const int x_orig = RouteDialog::channelDotsMargin;
  int x = x_orig;
  //int chan_y = RouteDialog::channelDotsMargin + (_isInput ? chans : 0);
  int chan_y = 2 * RouteDialog::channelDotsMargin;

  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::computeChannelYValues() col_width:%d chans_per_w:%d\n", col_width, chans_per_w);  // REMOVE Tim.
  
  int line_y = 2 * RouteDialog::channelLinesMargin + 
    (_isInput ? 0 : (RouteDialog::channelBarHeight + RouteDialog::channelDotsMargin + RouteDialog::channelLinesMargin));

  //QList<int> chan_ys;
  
  //// If it's a source, take care of the first batch of lines first, which are above the first channel bar.
  //if(_isInput)
  //{
//     for(int i = 0; i < chans; ++i)
//     {
//       const bool new_group = i && (i % chans_per_w == 0);
//       // Is it marked as used?
//       if(_channels.at(i)._connected)
//       {
//         // Set the value to an appropriate y value useful for drawing channel lines.
//         _channels[i]._lineY = line_y;
//         if(new_group)
//           line_y += RouteDialog::channelBarHeight;
//         else
//           line_y += RouteDialog::channelLinesSpacing;
//       }
//     }
  //}

  
  int cur_chan = 0;
  for(int i = 0; i < chans; )
  //for(int i = 0; i < chans; ++i)
  {
    //const bool new_group = i && (i % RouteDialog::channelDotsPerGroup == 0);
    //const bool new_section = i && (i % chans_per_w == 0);
    const bool is_connected = _channels.at(i)._connected;
    
    //if(new_section)
    //{  
    //  chan_y = line_y + RouteDialog::channelDotsMargin;
    //}
    
    // Is it marked as used?
    //if(_channelYValues.at(i) != -1)
    //if(_channels.at(i)._connected)
    if(is_connected)
    {
      // Replace the zero value with an appropriate y value useful for drawing channel lines.
      //_channelYValues.replace(i, y);
      _channels[i]._lineY = line_y;
      //if(new_section)
      //  line_y += RouteDialog::channelBarHeight;
      //else
      //  line_y += RouteDialog::channelLinesSpacing;
    }
    
//     if(_isInput)
//     {
//       // If we finished a section set button rects, or we reached the end
//       //  set the remaining button rects, based on current line y (and x).
//       if(new_section || i + 1 == chans)
//       {
//        for( ; cur_chan < i; ++cur_chan)
//        {
//          _channels[cur_chan]._buttonRect = QRect(x, chan_y, RouteDialog::channelDotDiameter, RouteDialog::channelDotDiameter);
//        }
//       }
//       
//     }
//     else
//     {
//       _channels[i]._buttonRect = QRect(x, chan_y, RouteDialog::channelDotDiameter, RouteDialog::channelDotDiameter);
//       
//     }

    if(!_isInput)
      _channels[i]._buttonRect = QRect(x, chan_y, RouteDialog::channelDotDiameter, RouteDialog::channelDotDiameter);
    
    ++i;
    const bool new_group = (i % RouteDialog::channelDotsPerGroup == 0);
    const bool new_section = (i % chans_per_w == 0);

    if(is_connected)
      line_y += RouteDialog::channelLineWidth + RouteDialog::channelLinesSpacing;
    
    if(_isInput)
    {
      // If we finished a section set button rects, or we reached the end
      //  set the remaining button rects, based on current line y (and x).
      if(new_section || i == chans)
      {
        x = x_orig;
        for( ; cur_chan < i; )
        {
          DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::computeChannelYValues() i:%d cur_chan:%d x:%d\n", i, cur_chan, x);  // REMOVE Tim.
          _channels[cur_chan]._buttonRect = QRect(x, line_y + RouteDialog::channelLinesMargin, RouteDialog::channelDotDiameter, RouteDialog::channelDotDiameter);
          ++cur_chan;
          x += RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing;
          if(cur_chan % RouteDialog::channelDotsPerGroup == 0)
            x += RouteDialog::channelDotGroupSpacing;
        }
        //line_y += RouteDialog::channelLinesMargin;
        //++cur_chan;
      }
    }
//     else
//     {
//       if(new_section)
//         x = x_orig;  // Reset
//       else
//       {
//         x += RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing;
//         if(new_group)
//           x += RouteDialog::channelDotGroupSpacing;
//       }
//     }

    if(new_section)
    {
      x = x_orig;  // Reset
//       chan_y = line_y + RouteDialog::channelLinesMargin + RouteDialog::channelDotsMargin;
//       chan_y = line_y + RouteDialog::channelLinesMargin;
      chan_y = line_y;
//       line_y += (RouteDialog::channelBarHeight + RouteDialog::channelLinesMargin);
      line_y += RouteDialog::channelBarHeight;
    }
    else
    {
      x += RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing;
      if(new_group)
        x += RouteDialog::channelDotGroupSpacing;
    }
  }
}

bool RouteTreeWidgetItem::mousePressHandler(QMouseEvent* e, const QRect& rect)
{
  const QPoint pt = e->pos(); 
  const Qt::KeyboardModifiers km = e->modifiers();
  bool ctl = false;
  switch(_itemMode)
  {
    case ExclusiveMode:
      ctl = false;
    break;
    case NormalMode:
      ctl = km & Qt::ControlModifier;
    break;
  }
  //bool shift = km & Qt::ShiftModifier;

//   RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(itemAt(pt));
//   bool is_cur = item && currentItem() && (item == currentItem());

  //if(is_cur)
  //  QTreeWidget::mousePressEvent(e);
  
  switch(type())
  {
    case NormalItem:
    case CategoryItem:
    case RouteItem:
    break;  
    case ChannelsItem:
      switch(_route.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          if(_route.track && _route.channel != -1) // && item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
          {
//             int chans;
//             if(_route.track->isMidiTrack())
//               chans = MIDI_CHANNELS;
//             else
//             {
//               MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(_route.track);
//               if(atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//                 chans = _isInput ? atrack->totalOutChannels() : atrack->totalInChannels();
//               else
//                 chans = atrack->channels();
//             }

            int ch = channelAt(pt, rect);
            
            //QBitArray ba = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
            //QBitArray ba_m = ba;
            //QBitArray ba_m = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
            //const int ba_sz = ba_m.size();
            const int ba_sz = _channels.size();
            bool changed = false;
            //if(!ctl)
            {
              //ba_m.fill(false);
              for(int i = 0; i < ba_sz; ++i)
              {
                //const bool b = ba_m.testBit(i);
                
                if(i == ch)
                {
                  if(ctl)
                  {
                    //_channels.toggleBit(i);
                    _channels[i].toggleSelected();
                    changed = true;
                  }
                  else
                  {
                    //if(!_channels.testBit(i))
                    if(!_channels.at(i)._selected)
                      changed = true;
                    //_channels.setBit(i);
                    _channels[i]._selected = true;
                  }
                }
                else if(!ctl)
                {
                  //if(_channels.testBit(i))
                  if(_channels. at(i)._selected)
                    changed = true;
                  //_channels.clearBit(i);
                  _channels[i]._selected = false;
                }
                  
  //               //if(ba_m.testBit(i))
  //               {
  //                 ba_m.clearBit(i);
  //                 changed = true;
  //               }
              }
            }
  //             //clearChannels();
  //           //  clearSelection();
  //           //int ch = channelAt(item, pt, chans);
  //           if(ch != -1 && ch < ba_sz)
  //           {
  //             ba_m.toggleBit(ch);
  //             changed = true;
  //           }

            //if(is_cur)
            //  QTreeWidget::mousePressEvent(e);
              
            //if(ba_m != ba)
//             if(changed)
//             {
//               item->setData(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole, qVariantFromValue<QBitArray>(ba_m));
//               //setCurrentItem(item);
//               update(visualItemRect(item));
//               //emit itemSelectionChanged();
//             }
            
//             //if(!is_cur)
//               QTreeWidget::mousePressEvent(e);

//             if(changed && is_cur)
//               //setCurrentItem(item);
//               emit itemSelectionChanged();
              
            //e->accept();
//             return;
            return changed;
          }
        break;
        case MusECore::Route::JACK_ROUTE:
        case MusECore::Route::MIDI_DEVICE_ROUTE:
        case MusECore::Route::MIDI_PORT_ROUTE:
        break;
      }
      
    break;  
  }

  return false;
  
//   QTreeWidget::mousePressEvent(e);
}

bool RouteTreeWidgetItem::mouseMoveHandler(QMouseEvent* e, const QRect& rect)
{
  const Qt::MouseButtons mb = e->buttons();
  if(mb != Qt::LeftButton)
    return false;
  
  const QPoint pt = e->pos(); 
  const Qt::KeyboardModifiers km = e->modifiers();
  
  bool ctl = false;
  switch(_itemMode)
  {
    case ExclusiveMode:
      ctl = false;
    break;
    case NormalMode:
      //ctl = true;
      //ctl = km & Qt::ControlModifier;
      ctl = km & Qt::ShiftModifier;
    break;
  }
  //bool shift = km & Qt::ShiftModifier;

//   RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(itemAt(pt));
//   bool is_cur = item && currentItem() && (item == currentItem());

  //if(is_cur)
  //  QTreeWidget::mousePressEvent(e);
  
  switch(type())
  {
    case NormalItem:
    case CategoryItem:
    case RouteItem:
    break;  
    case ChannelsItem:
      switch(_route.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          if(_route.track && _route.channel != -1) // && item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
          {
            int ch = channelAt(pt, rect);
            
            const int ba_sz = _channels.size();
            bool changed = false;
            for(int i = 0; i < ba_sz; ++i)
            {
              if(i == ch)
              {
//                 if(ctl)
//                 {
//                   _channels[i].toggleSelected();
//                   changed = true;
//                 }
//                 else
                {
                  if(!_channels.at(i)._selected)
                    changed = true;
                  _channels[i]._selected = true;
                }
              }
              else if(!ctl)
              {
                if(_channels. at(i)._selected)
                  changed = true;
                _channels[i]._selected = false;
              }
            }
            return changed;
          }
        break;
        case MusECore::Route::JACK_ROUTE:
        case MusECore::Route::MIDI_DEVICE_ROUTE:
        case MusECore::Route::MIDI_PORT_ROUTE:
        break;
      }
      
    break;  
  }

  return false;
  
//   QTreeWidget::mousePressEvent(e);
}

// bool RouteTreeWidgetItem::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
// {
//   //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint\n");  // REMOVE Tim.
// //   RouteDialog* router = qobject_cast< RouteDialog* >(parent());
//   //if(parent() && qobject_cast< RouteDialog* >(parent()))
// //   if(router)
// //   {
//     //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint parent is RouteDialog\n");  // REMOVE Tim.
//     //QWidget* qpd = qobject_cast<QWidget*>(painter->device());
//     //if(qpd)
//     if(painter->device())
//     {
//       //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint device is QWidget\n");  // REMOVE Tim.
//       //RouteDialog* router = static_cast<RouteDialog*>(parent());
//       
// //       if(index.column() == RouteDialog::ROUTE_NAME_COL && index.data(RouteDialog::RouteRole).canConvert<MusECore::Route>()) 
//       if(type() == ChannelsItem && index.column() == RouteDialog::ROUTE_NAME_COL) 
//       {
//         //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint data is Route\n");  // REMOVE Tim.
// //         MusECore::Route r = qvariant_cast<MusECore::Route>(index.data(RouteDialog::RouteRole));
//         QRect rect(option.rect);
// //         switch(r.type)
//         switch(_route.type)
//         {
//           case MusECore::Route::TRACK_ROUTE:
//             //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint route is track\n");  // REMOVE Tim.
//             if(_route.track && _route.channel != -1)
//             {
//               const int chans = _channels.size(); 
// //               int chans; 
// //               if(_route.track->isMidiTrack())
// //               {
// //                 //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint track is midi\n");  // REMOVE Tim.
// //                 chans = MIDI_CHANNELS;
// //               }
// //               else
// //               {
// //                 //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint track is audio\n");  // REMOVE Tim.
// //                 MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(_route.track);
// //                 if(atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH)
// //                 {
// //                   if(_isInput)
// //                     chans = atrack->totalOutChannels();
// //                   else
// //                     chans = atrack->totalInChannels();
// //                 }
// //                 else
// //                   chans = atrack->channels();
// //               }
//               
//               int w = RouteDialog::channelDotsMargin * 2 + RouteDialog::channelDotDiameter * chans;
//               if(chans > 1)
//                 w += RouteDialog::channelDotSpacing * (chans - 1);
//               if(chans > 4)
//                 w += RouteDialog::channelDotGroupSpacing * (chans - 1) / 4;
//               
//               //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint src list width:%d src viewport width:%d\n", router->newSrcList->width(), router->newSrcList->viewport()->width());  // REMOVE Tim.
//               //int x = _isInput ? router->newSrcList->viewport()->width() - w : RouteDialog::midiDotsMargin;
//               int x = _isInput ? painter->device()->width() - w : RouteDialog::channelDotsMargin;
//               const int y = RouteDialog::channelDotsMargin + (_isInput ? chans : 0);
//               
// //               QBitArray ba;
// //               int basize = 0;
// //               if(index.data(RouteDialog::ChannelsRole).canConvert<QBitArray>())
// //               {
// //                 ba = index.data(RouteDialog::ChannelsRole).value<QBitArray>();
// //                 basize = ba.size();
// //               }
//               
//               //const int y_sz = _channelYValues.size();
//               //const int y_sz = _channels.size();
//               const int connected_chans = connectedChannels();
//               int cur_chan_line = 0;
//               for(int i = 0; i < chans; )
//               {
//                 painter->setPen(Qt::black);
//                 //painter->drawRoundedRect(option.rect.x() + x, option.rect.y() + y, 
// //                 if(!ba.isNull() && i < basize && ba.testBit(i))
//                 //if(!_channels.isNull() && _channels.testBit(i))
//                 if(_channels.at(i)._selected)
//                   painter->fillRect(x, option.rect.y() + y, 
//                                            RouteDialog::channelDotDiameter, RouteDialog::channelDotDiameter,
//                                            option.palette.highlight());
//                 //else
//                   painter->drawRoundedRect(x, option.rect.y() + y, 
//                                            RouteDialog::channelDotDiameter, RouteDialog::channelDotDiameter,
//                                            30, 30);
//                 if((i % 2) == 0)
//                   painter->setPen(Qt::darkGray);
//                 else
//                   painter->setPen(Qt::black);
//                 const int xline = x + RouteDialog::channelDotDiameter / 2;
//                 //if(i < y_sz)
//                 if(_channels.at(i)._connected)
//                 {
//                   //const int chan_y = _channelYValues.at(i);
//                   const int chan_y = _channels.at(i)._lineY;
//                   // -1 means not connected.
//                   //if(chan_y != -1)
//                   //{
//                     if(_isInput)
//                     {
//                       //const int yline = option.rect.y() + y;
//                       //painter->drawLine(xline, yline, xline, yline - chans + i);
//                       //painter->drawLine(xline, yline - chans + i, painter->device()->width(), yline - chans + i);
//                       const int yline = option.rect.y() + chan_y;
//                       painter->drawLine(xline, yline, xline, yline - connected_chans + cur_chan_line);
//                       painter->drawLine(xline, yline - connected_chans + cur_chan_line, painter->device()->width(), yline - connected_chans + cur_chan_line);
//                     }
//                     else
//                     {
//                       //const int yline = option.rect.y() + RouteDialog::midiDotsMargin + RouteDialog::midiDotDiameter;
//                       //painter->drawLine(xline, yline, xline, yline + i);
//                       //painter->drawLine(0, yline + i, xline, yline + i);
//                       const int yline = option.rect.y() + RouteDialog::channelDotsMargin + RouteDialog::channelDotDiameter;
//                       painter->drawLine(xline, yline, xline, yline + chan_y);
//                       painter->drawLine(0, yline + chan_y, xline, yline + chan_y);
//                     }
//                     ++cur_chan_line;
//                   //}
//                 }
//                 
//                 ++i;
//                 x += RouteDialog::channelDotDiameter + RouteDialog::channelDotSpacing;
//                 if(i && ((i % 4) == 0))
//                   x += RouteDialog::channelDotGroupSpacing;
//               }
//               return true;
//             }
//           break;  
//           case MusECore::Route::MIDI_DEVICE_ROUTE:
//           case MusECore::Route::MIDI_PORT_ROUTE:
//           case MusECore::Route::JACK_ROUTE:
//           break;  
//         }
//       }
//     }
// //   }
// //   QStyledItemDelegate::paint(painter, option, index);
//   return false;
// }

bool RouteTreeWidgetItem::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
  {
    if(index.column() == RouteDialog::ROUTE_NAME_COL)
    {
      RouteTreeWidget* rtw = qobject_cast<RouteTreeWidget*>(treeWidget());
      if(!rtw)
        return false;
      
      switch(type())
      {
        case ChannelsItem:
        {
          if(!treeWidget()->viewport())
            return false;

          const int col_width = rtw->columnWidth(index.column()); 
          const int view_width = rtw->viewport()->width();
          const int chans = _channels.size();
          const int view_offset = rtw->header()->offset();
          //const int x_offset = (_isInput ? view_width - getSizeHint(index.column(), col_width).width() + view_offset : -view_offset);
          //const int x_offset = (_isInput ? view_width - getSizeHint(index.column(), col_width).width() - view_offset : -view_offset);
      //       const int x_offset = (_isInput ? view_width - getSizeHint(index.column(), view_width).width() - view_offset : -view_offset);
          const int x_offset = (_isInput ? 
                                //view_width - _channels.widthHint(rtw->wordWrap() ? view_width : -1) - view_offset : -view_offset);
                                col_width - _channels.widthHint(rtw->channelWrap() ? col_width : -1) - view_offset : -view_offset);

          DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::paint() rect x:%d y:%d w:%d h:%d view_offset:%d x_offset:%d dev w:%d col w:%d header w:%d view w:%d\n", 
                  option.rect.x(), option.rect.y(), option.rect.width(), option.rect.height(), view_offset, x_offset, painter->device()->width(), 
                  rtw->columnWidth(index.column()), rtw->header()->sectionSize(index.column()), view_width);  // REMOVE Tim.
      
          
          // From QStyledItemDelegate::paint help: Neccessary?
          // "After painting, you should ensure that the painter is returned to its the state it was supplied in when this function
          //  was called. For example, it may be useful to call QPainter::save() before painting and QPainter::restore() afterwards."
          painter->save();
          
          // Need to be able to paint beyond the right edge of the column width, 
          //  all the way to the view's right edge.
          //painter->setClipRect(option.rect);
          QRect clip_rect(option.rect);
          clip_rect.setWidth(view_width - option.rect.x());
          painter->setClipRect(clip_rect);
          
          if(index.parent().isValid() && (index.parent().row() & 0x01))
            painter->fillRect(option.rect, option.palette.alternateBase());
          int cur_chan = 0;
          //QColor color;
//           QBrush brush;
          QPen pen;

          // Set a small five-pixel font size for the numbers inside the dots.
          QFont fnt = font(index.column());
          //fnt.setStyleStrategy(QFont::NoAntialias);
          //fnt.setStyleStrategy(QFont::PreferBitmap);
          // -2 for the border, -2 for the margin, and +1 for setPixelSize which seems to like it.
          //fnt.setPixelSize(RouteDialog::channelDotDiameter - 2 - 2 + 1); 
          fnt.setPixelSize(RouteDialog::channelDotDiameter / 2 + 1); 
          painter->setFont(fnt);
          for(int i = 0; i < chans; ++i)
          {
            const RouteChannelsStruct& ch_struct = _channels.at(i);
            const QRect& ch_rect = ch_struct._buttonRect;
            
            QPainterPath path;
            path.addRoundedRect(x_offset + ch_rect.x(), option.rect.y() + ch_rect.y(), 
                                ch_rect.width(), ch_rect.height(), 
                                30, 30);
            if(ch_struct._selected)
              painter->fillPath(path, option.palette.highlight());
            //painter->setPen(ch_struct._selected ? option.palette.highlightedText().color() : option.palette.text().color());
            //painter->setPen(ch_struct._routeSelected ? Qt::yellow : option.palette.text().color());
            painter->setPen(option.palette.text().color());
            painter->drawPath(path);

            //const int ch_num = (i + 1) % 10;
            if(chans > RouteDialog::channelDotsPerGroup)
            {
              //if((i % RouteDialog::channelDotsPerGroup) == 0 || ((i + 1) % 10 == 0))
              if((i % RouteDialog::channelDotsPerGroup) == 0)
              {
                painter->setPen(ch_struct._selected ? option.palette.highlightedText().color() : option.palette.text().color());
                painter->drawText(x_offset + ch_rect.x(), option.rect.y() + ch_rect.y(), 
                                 ch_rect.width(), ch_rect.height(), 
                                 Qt::AlignCenter, 
                                 //QString::number((ch_num + 1) / 10));
                                 QString::number(i + 1));
              }
            }
            
            if(ch_struct._connected)
            {
//             if((cur_chan % 2) == 0)
//               painter->setPen(option.palette.text().color().lighter());
//             else
//               painter->setPen(option.palette.text().color());
              //painter->setPen(ch_struct._selected ? option.palette.highlight().color() : option.palette.text().color());
              
//               if(ch_struct._routeSelected)
//                 brush = Qt::yellow;
//                 //brush = Qt::red;
//                 //brush = QColor(0, 255, 255);
//                 //brush = option.palette.highlight();
//               else if(ch_struct._selected)
//                 brush = option.palette.highlight();
//               else 
//                 brush = option.palette.text();

  //             painter->setPen(ch_struct._selected ? option.palette.highlight().color() : option.palette.text().color());
//               painter->setPen(color);

              const int line_x = x_offset + ch_rect.x() + RouteDialog::channelDotDiameter / 2;
              const int line_y = option.rect.y() + ch_struct._lineY;
              if(_isInput)
              {
                const int ch_y = option.rect.y() + ch_rect.y() -1;
                DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::paint() input: line_x:%d ch_y:%d line_y:%d view_w:%d\n", line_x, ch_y, line_y, view_width);  // REMOVE Tim.
                pen.setBrush((ch_struct._selected && !ch_struct._routeSelected) ? option.palette.highlight() : option.palette.text());
                pen.setStyle(Qt::SolidLine);
                painter->setPen(pen);
                painter->drawLine(line_x, ch_y, line_x, line_y);
                painter->drawLine(line_x, line_y, view_width, line_y);
                if(ch_struct._routeSelected)
                {
                  pen.setBrush(Qt::yellow);
                  pen.setStyle(Qt::DotLine);
                  painter->setPen(pen);
                  painter->drawLine(line_x, ch_y, line_x, line_y);
                  painter->drawLine(line_x, line_y, view_width, line_y);
                }
              }
              else
              {
                const int ch_y = option.rect.y() + ch_rect.y() + ch_rect.height();
                pen.setBrush((ch_struct._selected && !ch_struct._routeSelected) ? option.palette.highlight() : option.palette.text());
                pen.setStyle(Qt::SolidLine);
                painter->setPen(pen);
                painter->drawLine(line_x, ch_y, line_x, line_y);
                painter->drawLine(x_offset, line_y, line_x, line_y);
                if(ch_struct._routeSelected)
                {
                  pen.setBrush(Qt::yellow);
                  pen.setStyle(Qt::DotLine);
                  painter->setPen(pen);
                  painter->drawLine(line_x, ch_y, line_x, line_y);
                  painter->drawLine(x_offset, line_y, line_x, line_y);
                }
              }
              ++cur_chan;
            }
          }
          painter->restore();
          return true;
        }
        break;
        
        case CategoryItem:
        case RouteItem:
        {
          if(const QStyle* st = rtw->style())
          {
            st = st->proxy();
            painter->save();
            painter->setClipRect(option.rect);
            
            const QRect cb_rect = st->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option);
            const QRect ico_rect = st->subElementRect(QStyle::SE_ItemViewItemDecoration, &option);
            const QRect text_rect = st->subElementRect(QStyle::SE_ItemViewItemText, &option);

            // Draw the row background (alternating colours etc.)
            QPalette::ColorGroup cg = (/* widget ? widget->isEnabled() : */ (option.state & QStyle::State_Enabled)) ?
                                      QPalette::Normal : QPalette::Disabled;
            if(cg == QPalette::Normal && !(option.state & QStyle::State_Active))
              cg = QPalette::Inactive;
            if((option.state & QStyle::State_Selected) && st->styleHint(QStyle::SH_ItemView_ShowDecorationSelected, &option /*, widget*/))
              painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
            //else if(option.features & QStyleOptionViewItem::Alternate)
            // Hm, something else draws the alternating colours, no control over it here. 
            // Disabled it in the UI so it does not interfere here.
            //else if(treeWidget()->alternatingRowColors() && (index.row() & 0x01))
            else if((index.row() & 0x01))
              painter->fillRect(option.rect, option.palette.brush(cg, QPalette::AlternateBase));
            
            // Draw the item background.
            st->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter);
            
            // Draw the check mark
            if(option.features & QStyleOptionViewItem::HasCheckIndicator) 
            {
              QStyleOptionViewItem opt(option);
              opt.rect = cb_rect;
              opt.state = opt.state & ~QStyle::State_HasFocus;
              switch(option.checkState) 
              {
                case Qt::Unchecked:
                    opt.state |= QStyle::State_Off;
                break;
                case Qt::PartiallyChecked:
                    opt.state |= QStyle::State_NoChange;
                break;
                case Qt::Checked:
                    opt.state |= QStyle::State_On;
                break;
              }
              st->drawPrimitive(QStyle::PE_IndicatorViewItemCheck, &opt, painter);
            }
            
            // Draw the icon.
            QIcon::Mode mode = QIcon::Normal;
            if(!(option.state & QStyle::State_Enabled))
              mode = QIcon::Disabled;
            else if(option.state & QStyle::State_Selected)
              mode = QIcon::Selected;
            QIcon::State state = option.state & QStyle::State_Open ? QIcon::On : QIcon::Off;
            option.icon.paint(painter, ico_rect, option.decorationAlignment, mode, state);
            
            // Draw the text.
            st->drawItemText(painter, 
                              text_rect, 
                              //textAlignment(index.column()) | Qt::TextWordWrap | Qt::TextWrapAnywhere, 
                              option.displayAlignment | (rtw->wordWrap() ? (Qt::TextWordWrap | Qt::TextWrapAnywhere) : 0), 
                              //treeWidget()->palette(), 
                              option.palette, 
                              //!isDisabled(), 
                              option.state & QStyle::State_Enabled, 
                              //text(index.column()),
                              rtw->wordWrap() ? 
                                option.text : option.fontMetrics.elidedText(option.text, rtw->textElideMode(), text_rect.width()),
                              //isSelected() ? QPalette::HighlightedText : QPalette::Text
                              (option.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text
                              );
            
            // Draw the focus.
            if(option.state & QStyle::State_HasFocus)
            {
              QStyleOptionFocusRect o;
              o.QStyleOption::operator=(option);
              o.rect = st->subElementRect(QStyle::SE_ItemViewItemFocusRect, &option);
              o.state |= QStyle::State_KeyboardFocusChange;
              o.state |= QStyle::State_Item;
              QPalette::ColorGroup cg = 
                                  (option.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
              o.backgroundColor = option.palette.color(cg, 
                                  (option.state & QStyle::State_Selected) ? QPalette::Highlight : QPalette::Window);
              st->drawPrimitive(QStyle::PE_FrameFocusRect, &o, painter);
            }
            
            painter->restore();
            return true;
          }
        }
        break;
        
        case NormalItem:
        break;
      }
    }
  }
  return false;
}

// QSize RouteTreeWidgetItem::getSizeHint(int col, int col_width) const
// {
//   DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::getSizeHint width:%d view width:%d col:%d column width:%d\n",
//           treeWidget()->viewport()->width(), treeWidget()->width(), col, treeWidget()->columnWidth(RouteDialog::ROUTE_NAME_COL));  // REMOVE Tim.
//   
//   if(col_width == -1)
//     col_width = treeWidget()->viewport()->width();
//             
//   if(col == RouteDialog::ROUTE_NAME_COL) 
//   {
//       switch(type())
//       {
//         case ChannelsItem:
//           return _channels.sizeHint(col_width);
//         break;
//         
//         case NormalItem:
//         break;
//         
//         case CategoryItem:
//         case RouteItem:
//         {
//           QStyle* st = treeWidget()->style();
//           if(st)
//           {
//             // Qt sources show itemTextRect() just calls QFontMetrics::boundingRect and supports enabled.
//             // And Qt::TextWrapAnywhere is not listed as supported in boundingRect() help, yet it is in drawItemText().
//             // A look through the Qt sources shows it IS supported. Try it...
//             QRect r = st->itemTextRect(treeWidget()->fontMetrics(), QRect(0, 0, col_width, 32767), 
//                                        textAlignment(RouteDialog::ROUTE_NAME_COL) | Qt::TextWordWrap | Qt::TextWrapAnywhere,
//                                        !isDisabled(), text(RouteDialog::ROUTE_NAME_COL));
//             return r.size();
//           }
//         }
//         break;
//       }
//     
// //     switch(_route.type)
// //     {
// //       case MusECore::Route::TRACK_ROUTE:
// //         if(_route.track && _route.channel != -1)
// //         {
// //           //int chans; 
// //           const int chans = _channels.size(); 
// // //           if(_route.track->isMidiTrack())
// // //             chans = MIDI_CHANNELS;
// // //           else
// // //           {
// // //             MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(_route.track);
// // //             if(atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH)
// // //             {
// // //               if(_isInput)
// // //                 chans = atrack->totalOutChannels();
// // //               else
// // //                 chans = atrack->totalInChannels();
// // //             }
// // //             else
// // //               chans = atrack->channels();
// // //           }
// //           
// // //           int w = RouteDialog::midiDotsMargin * 2 + RouteDialog::midiDotDiameter * chans;
// // //           if(chans > 1)
// // //             w += RouteDialog::midiDotSpacing * (chans - 1);
// // //           if(chans > RouteDialog::midiDotsPerGroup)
// // //             w += RouteDialog::midiDotGroupSpacing * (chans - 1) / RouteDialog::midiDotsPerGroup;
// //           int w = col_width;
// //           const int h = RouteDialog::midiDotDiameter + RouteDialog::midiDotsMargin * 2 + chans;
// //           return QSize(w, h);
// //         }
// //       break;  
// //       case MusECore::Route::MIDI_DEVICE_ROUTE:
// //       case MusECore::Route::MIDI_PORT_ROUTE:
// //       case MusECore::Route::JACK_ROUTE:
// //       break;  
// //     }
//   }
//   
//   //return QStyledItemDelegate::sizeHint(option, index);
//   return QSize();
//   //return sizeHint(col);
// }

//QSize RouteTreeWidgetItem::getSizeHint(const QStyleOptionViewItem& option, const QModelIndex &index) const
QSize RouteTreeWidgetItem::getSizeHint(int column, int width) const
{
//     if (index.data().canConvert<StarRating>()) {
//         StarRating starRating = qvariant_cast<StarRating>(index.data());
//         return starRating.sizeHint();
//     } else

//   if(index.column() == ControlMapperDialog::C_COLOR)
//     return QSize(__COLOR_CHOOSER_ELEMENT_WIDTH__ * __COLOR_CHOOSER_NUM_COLUMNS__,
//                  __COLOR_CHOOSER_ELEMENT_HEIGHT__ * (__COLOR_CHOOSER_NUM_ELEMENTS__ / __COLOR_CHOOSER_NUM_COLUMNS__));
//     
  //return QStyledItemDelegate::sizeHint(option, index);

//   const QSize sz = getSizeHint(index.column());
//   DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::sizeHint opt rect x:%d y:%d w:%d h:%d  hint w:%d h:%d column width:%d\n",
//           option.rect.x(), option.rect.y(), option.rect.width(), option.rect.height(), 
//           sz.width(), sz.height(), 
//           treeWidget()->columnWidth(RouteDialog::ROUTE_NAME_COL));  // REMOVE Tim.
//   return sz;


//   return getSizeHint(index.column());
  

//   const int col = index.column();
//   int width = option.rect.width();
  
  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::getSizeHint width:%d col:%d column width:%d\n",
           treeWidget()->width(), column, treeWidget()->columnWidth(column));  // REMOVE Tim.
  
//   if(width <= 0)
//     width = treeWidget()->viewport()->width();
//   if(width <= 0)
//     width = treeWidget()->columnWidth(column);
            
  if(column == RouteDialog::ROUTE_NAME_COL) 
  {
    RouteTreeWidget* rtw = qobject_cast<RouteTreeWidget*>(treeWidget());
    if(!rtw)
      return QSize();
    
    switch(type())
    {
      case NormalItem:
      break;
      
      case ChannelsItem:
        //fprintf(stderr, "RouteTreeWidgetItem::getSizeHint ChannelsItem w:%d\n", width); // REMOVE Tim.
        return _channels.sizeHint(rtw->channelWrap() ? width : -1);
      break;
      
      case CategoryItem:
      case RouteItem:
      {
        if(!rtw->wordWrap())
          return QSize();
        
        if(const QStyle* st = rtw->style())
        {
          st = st->proxy();
          QStyleOptionViewItem vopt;
          vopt.features = QStyleOptionViewItem::None;
          
          vopt.text = text(column);
          vopt.rect = QRect(0, 0, rtw->wordWrap() ? width : _VERY_LARGE_INTEGER_, -1);
          vopt.displayAlignment = Qt::Alignment(textAlignment(column));

          if(icon(column).isNull())
            vopt.decorationSize = QSize();
          else
          {
            vopt.features |= QStyleOptionViewItem::HasDecoration;
            vopt.decorationSize = rtw->iconSize();
            vopt.icon = icon(column);
          }
          
          if(rtw->wordWrap())
            vopt.features |= QStyleOptionViewItem::WrapText;
          vopt.features |= QStyleOptionViewItem::HasDisplay;
          
          vopt.font = font(column);
          vopt.fontMetrics = rtw->fontMetrics();
          
          vopt.state = QStyle::State_Active;
          if(!isDisabled())
            vopt.state |= QStyle::State_Enabled;
          if(flags() & Qt::ItemIsUserCheckable)
          {
            vopt.features |= QStyleOptionViewItem::HasCheckIndicator;
            vopt.checkState = checkState(column);
            if(checkState(column) == Qt::Unchecked)
              vopt.state |= QStyle::State_Off;
            else if(checkState(column) == Qt::Checked)
              vopt.state |= QStyle::State_On;
          }
          
          if(isSelected())
            vopt.state |= QStyle::State_Selected;
          
          QSize ct_sz = st->sizeFromContents(QStyle::CT_ItemViewItem, &vopt, QSize(rtw->wordWrap() ? width : _VERY_LARGE_INTEGER_, -1));
          const QRect text_rect = st->subElementRect(QStyle::SE_ItemViewItemText, &vopt);
          QRect r = st->itemTextRect(//treeWidget()->fontMetrics(),
                                     vopt.fontMetrics,
                                     text_rect, 
                                     //textAlignment(column) | Qt::TextWordWrap | Qt::TextWrapAnywhere,
                                     vopt.displayAlignment | Qt::TextWordWrap | Qt::TextWrapAnywhere,
                                     //!isDisabled(), 
                                     vopt.state & QStyle::State_Enabled, 
                                     //text(column));
                                     vopt.text);
          if(r.height() > ct_sz.height())
            ct_sz.setHeight(r.height());

          return ct_sz;
        }
      }
      break;
    }
  }
  
  return QSize();
}
 
//   //if(index.column() == RouteDialog::ROUTE_NAME_COL && index.data(RouteDialog::RouteRole).canConvert<MusECore::Route>()) 
//   if(type() == ChannelsItem && index.column() == RouteDialog::ROUTE_NAME_COL) 
//   {
//     switch(_route.type)
//     {
//       case MusECore::Route::TRACK_ROUTE:
//         if(_route.track && _route.channel != -1)
//         {
//           //int chans; 
//           const int chans = _channels.size(); 
// //           if(_route.track->isMidiTrack())
// //             chans = MIDI_CHANNELS;
// //           else
// //           {
// //             MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(_route.track);
// //             if(atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH)
// //             {
// //               if(_isInput)
// //                 chans = atrack->totalOutChannels();
// //               else
// //                 chans = atrack->totalInChannels();
// //             }
// //             else
// //               chans = atrack->channels();
// //           }
//           int w = RouteDialog::midiDotsMargin * 2 + RouteDialog::midiDotDiameter * chans;
//           if(chans > 1)
//             w += RouteDialog::midiDotSpacing * (chans - 1);
//           if(chans > 4)
//             w += RouteDialog::midiDotGroupSpacing * (chans - 1) / 4;
//           const int h = RouteDialog::midiDotDiameter + RouteDialog::midiDotsMargin * 2 + chans;
//           return QSize(w, h);
//         }
//       break;  
//       case MusECore::Route::MIDI_DEVICE_ROUTE:
//       case MusECore::Route::MIDI_PORT_ROUTE:
//       case MusECore::Route::JACK_ROUTE:
//       break;  
//     }
//   }
//   //return QStyledItemDelegate::sizeHint(option, index);
//   //return QSize();
//   return sizeHint(index.column());
// }

// void RouteTreeWidgetItem::columnSizeChanged(int logicalIndex, int oldSize, int newSize)
// {
//   DEBUG_PRST_ROUTES(stderr, "RouteTreeWidgetItem::columnSizeChanged idx:%d old sz:%d new sz:%d\n", logicalIndex, oldSize, newSize);
//   if(type() == ChannelsItem && logicalIndex == RouteDialog::ROUTE_NAME_COL)
//   {
//     //setSizeHint(logicalIndex, getSizeHint(logicalIndex));
//   }
// }

//bool RouteTreeWidgetItem::testForRelayout(const QStyleOptionViewItem& /*option*/, const QModelIndex& index, int old_width, int new_width) const
bool RouteTreeWidgetItem::testForRelayout(int column, int old_width, int new_width)
{
  switch(type())
  {
    case NormalItem:
    break;
    
    case CategoryItem:
    case RouteItem:
    {
      //if(index.column() == RouteDialog::ROUTE_NAME_COL)
      if(column == RouteDialog::ROUTE_NAME_COL)
      {
//         if(const QStyle* st = treeWidget()->style())
//         {
//           st = st->proxy();
//           // Works fine with TextWrapAnywhere. The -1 represents 'infinite' vertical space - 
//           //  itemTextRect doesn't seem to care in this case with wrap anywhere.
//           QRect old_r = st->itemTextRect(treeWidget()->fontMetrics(), 
//                                         QRect(0, 0, old_width, -1),
//                                         textAlignment(RouteDialog::ROUTE_NAME_COL) | Qt::TextWordWrap | Qt::TextWrapAnywhere,
//                                         !isDisabled(), text(RouteDialog::ROUTE_NAME_COL));
//           QRect new_r = st->itemTextRect(treeWidget()->fontMetrics(), 
//                                         QRect(0, 0, new_width, -1),
//                                         textAlignment(RouteDialog::ROUTE_NAME_COL) | Qt::TextWordWrap | Qt::TextWrapAnywhere,
//                                         !isDisabled(), text(RouteDialog::ROUTE_NAME_COL));
//           return new_r.height() != old_r.height();
//         }
//         return new_sz.height() != old_sz.height();
        
        //if(MusEGlobal::config.routerExpandVertically)
        if(!treeWidget()->wordWrap())
          return false;
        
        return getSizeHint(column, new_width).height() != getSizeHint(column, old_width).height();
      }
    }
    break;
    
    case ChannelsItem:
    {
      //if(index.column() == RouteDialog::ROUTE_NAME_COL)
      if(column == RouteDialog::ROUTE_NAME_COL)
      {
//         // If the width hints are different we must (at least) update the channels' button rectangles.
//         if(_channels.widthHint(new_width) != _channels.widthHint(old_width))
//           computeChannelYValues(new_width);
//         // If the height hints are different we must trigger a relayout.
//         return _channels.heightHint(new_width) != _channels.heightHint(old_width);
        
        RouteTreeWidget* rtw = qobject_cast<RouteTreeWidget*>(treeWidget());
        if(!rtw)
          return false;
    
        if(!rtw->channelWrap())
          return false;
        
        const QSize old_sz = getSizeHint(column, old_width);
        const QSize new_sz = getSizeHint(column, new_width);
        // If the width hints are different we must (at least) update the channels' button rectangles.
        if(new_sz.width() != old_sz.width())
          computeChannelYValues(new_width);
        // If the height hints are different we must trigger a relayout.
        return new_sz.height() != old_sz.height();
      }
    }
    break;
  }
  return false;
}  
  
// //   if(type() == ChannelsItem && col == RouteDialog::ROUTE_NAME_COL)
//   if(type() == ChannelsItem && index.column() == RouteDialog::ROUTE_NAME_COL)
//   {
// //     const QSize old_sz = getSizeHint(col, old_width);
// //     const QSize new_sz = getSizeHint(col, new_width);
//     
//     const QSize old_sz = getSizeHint(option, index);
//     const QSize new_sz = getSizeHint(option, index);
//     
//     
//     //return old_sz.isValid() && new_sz.isValid() && old_sz.height() != new_sz.height();
//     //return old_sz.isValid() && new_sz.isValid() && old_sz != new_sz;
//     return old_sz != new_sz;
//   }
//   return false;
// }

bool RouteTreeWidgetItem::routeNodeExists()
{
  switch(type())
  {
    case CategoryItem:
    case NormalItem:
      return true;
    break;
    
    case RouteItem:
    case ChannelsItem:
      return _route.exists();
    break;
  }
  return false;
}


//-----------------------------------
//   ConnectionsView
//-----------------------------------

ConnectionsView::ConnectionsView(QWidget* parent, RouteDialog* d)
        : QFrame(parent), _routeDialog(d)
{
  lastY = 0;
  setMinimumWidth(20);
  //setMaximumWidth(120);
  setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
}

ConnectionsView::~ConnectionsView()
{
}

int ConnectionsView::itemY(RouteTreeWidgetItem* item, bool /*is_input*/, int channel) const
{
//   if(item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//   {        
//     const MusECore::Route r = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//   }
  
  
  QRect rect;
  QTreeWidget* tree = item->treeWidget();
  //QTreeWidgetItem* parent = item->parent();

  QTreeWidgetItem* top_closed = 0;
  QTreeWidgetItem* parent = item;
  while(parent)
  {
    parent = parent->parent();
    if(!parent)
      break;
    if(!parent->isExpanded())
      top_closed = parent;
  }
  
  const int line_width = _routeDialog->newSrcList->lineWidth();
  
  //if(parent && !parent->isExpanded()) 
  if(top_closed) 
  {
    rect = tree->visualItemRect(top_closed);
    return line_width + rect.top() + rect.height() / 2;
  } 
//   else 
//   {
    rect = tree->visualItemRect(item);
    if(channel != -1)
      return line_width + rect.top() + item->channelYValue(channel);
    return line_width + rect.top() + rect.height() / 2;
//   }
  //DEBUG_PRST_ROUTES(stderr, "ConnectionsView::itemY: left:%d top:%d right:%d bottom:%d\n", rect.left(), rect.top(), rect.right(), rect.bottom());
//   if(channel != -1)
//     //return rect.top() + RouteDialog::channelDotsMargin + (is_input ? 0 : RouteDialog::channelDotDiameter) + channel;
//     return rect.top() + item->channelYValue(channel);
//   
//   return rect.top() + rect.height() / 2;
}


void ConnectionsView::drawConnectionLine(QPainter* pPainter,
        int x1, int y1, int x2, int y2, int h1, int h2 )
{
  //DEBUG_PRST_ROUTES(stderr, "ConnectionsView::drawConnectionLine: x1:%d y1:%d x2:%d y2:%d h1:%d h2:%d\n", x1, y1, x2, y2, h1, h2);
  
  // Account for list view headers.
  y1 += h1;
  y2 += h2;

  // Invisible output ports don't get a connecting dot.
  if(y1 > h1)
    pPainter->drawLine(x1, y1, x1 + 4, y1);

  // How do we'll draw it? // TODO
  if(1) 
  {
    // Setup control points
    QPolygon spline(4);
    const int cp = int(float(x2 - x1 - 8) * 0.4f);
    spline.putPoints(0, 4,
            x1 + 4, y1, x1 + 4 + cp, y1, 
            x2 - 4 - cp, y2, x2 - 4, y2);
    // The connection line, it self.
    QPainterPath path;
    path.moveTo(spline.at(0));
    path.cubicTo(spline.at(1), spline.at(2), spline.at(3));
    pPainter->strokePath(path, pPainter->pen());
  }
  else 
    pPainter->drawLine(x1 + 4, y1, x2 - 4, y2);

  // Invisible input ports don't get a connecting dot.
  if(y2 > h2)
    pPainter->drawLine(x2 - 4, y2, x2, y2);
}

void ConnectionsView::drawItem(QPainter* painter, QTreeWidgetItem* routesItem, const QColor& col)
{
  const int yc = QWidget::pos().y();
  const int yo = _routeDialog->newSrcList->pos().y();
  const int yi = _routeDialog->newDstList->pos().y();
  const int x1 = 0;
  const int x2 = QWidget::width();
  const int h1 = (_routeDialog->newSrcList->header())->sizeHint().height();
  const int h2 = (_routeDialog->newDstList->header())->sizeHint().height();
  int y1;
  int y2;
  QPen pen;
  const int pen_wid_norm = 0;
  const int pen_wid_wide = 3;
  
  if(routesItem->data(RouteDialog::ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && 
     routesItem->data(RouteDialog::ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
  {        
    const MusECore::Route src = routesItem->data(RouteDialog::ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>();
    const MusECore::Route dst = routesItem->data(RouteDialog::ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
    RouteTreeWidgetItem* srcItem = _routeDialog->newSrcList->findItem(src);
    if(srcItem)
    {
      RouteTreeWidgetItem* dstItem = _routeDialog->newDstList->findItem(dst);
      if(dstItem)
      {
        int src_chan = src.channel;
        int dst_chan = dst.channel;
        bool src_wid = false;
        bool dst_wid = false;
        switch(src.type)
        {
          case MusECore::Route::TRACK_ROUTE:
            // Don't draw if channel is unavailable.
            if(src_chan >= srcItem->channelCount())
              return;
            if(src_chan == -1 && src.channels == -1) 
              src_wid = true;
          break;
          
          case MusECore::Route::MIDI_DEVICE_ROUTE:
          case MusECore::Route::MIDI_PORT_ROUTE:
            if(src_chan == -1 && src.channels == -1) 
              src_wid = true;
            // Support port/device items (no channel bar) to track channel item routes:
            // Set source channel to -1 so it draws to the vertical middle of the item.
            src_chan = -1;
          break;
          
          case MusECore::Route::JACK_ROUTE:
          break;
        }
        switch(dst.type)
        {
          case MusECore::Route::TRACK_ROUTE:
            // Don't draw if channel is unavailable.
            if(dst_chan >= dstItem->channelCount())
              return;
            if(dst_chan == -1 && dst.channels == -1) 
              dst_wid = true;
          break;
          
          case MusECore::Route::MIDI_DEVICE_ROUTE:
          case MusECore::Route::MIDI_PORT_ROUTE:
            if(dst_chan == -1 && dst.channels == -1) 
              dst_wid = true;
            // Support track channel items to port/device items (no channel bar) routes:
            // Set dest channel to -1 so it draws to the vertical middle of the item.
            dst_chan = -1;
          break;
          
          case MusECore::Route::JACK_ROUTE:
          break;
        }

        if(src_wid && dst_wid) 
          pen.setWidth(pen_wid_wide);
        else
          pen.setWidth(pen_wid_norm);
        
        pen.setColor(col);
        painter->setPen(pen);
        y1 = itemY(srcItem, true, src_chan) + (yo - yc);
        y2 = itemY(dstItem, false, dst_chan) + (yi - yc);
        drawConnectionLine(painter, x1, y1, x2, y2, h1, h2);
      }
//       else
//       {
//         fprintf(stderr, "ConnectionsView::drawItem: dstItem not found:\n");
//         src.dump();
//         dst.dump();
//       }
    }
//     else
//     {
//       fprintf(stderr, "ConnectionsView::drawItem: srcItem not found:\n");
//       src.dump();
//       dst.dump();
//     }
  }
}

// Draw visible port connection relation arrows.
void ConnectionsView::paintEvent(QPaintEvent*)
{
  //DEBUG_PRST_ROUTES(stderr, "ConnectionsView::paintEvent: _routeDialog:%p\n", _routeDialog);
  if(!_routeDialog)
    return;

  QPainter painter(this);
//   int i, rgb[3] = { 0x33, 0x66, 0x99 };
  int i, rgb[3] = { 0x33, 0x58, 0x7f };
  //int i, rgb[3] = { 0x00, 0x2c, 0x7f };

  // Inline adaptive to darker background themes...
  if(QWidget::palette().window().color().value() < 0x7f)
    for (i = 0; i < 3; ++i) 
      //rgb[i] += 0x33;
      //rgb[i] += 0x66;
      rgb[i] += 0x80;

  i = 0;
//   const int x1 = 0;
//   const int x2 = QWidget::width();
//   const int h1 = (_routeDialog->newSrcList->header())->sizeHint().height();
//   const int h2 = (_routeDialog->newDstList->header())->sizeHint().height();
//   QPen pen;
//   const int pen_wid_norm = 0;
//   const int pen_wid_wide = 3;
//   
//   QTreeWidgetItem* src_sel = 0;
//   QTreeWidgetItem* dst_sel = 0;
//   int src_sel_ch = -1;
//   int dst_sel_ch = -1;
//   bool sel_wid = false;
  
//   QTreeWidgetItem* cur_item = _routeDialog->routeList->currentItem();
  const int iItemCount = _routeDialog->routeList->topLevelItemCount();
  // Draw unselected items behind selected items.
  for(int iItem = 0; iItem < iItemCount; ++iItem, ++i) 
  {
    QTreeWidgetItem* item = _routeDialog->routeList->topLevelItem(iItem);
    //++i;
    //if(!item)
    if(!item || item->isHidden() || item->isSelected())
      continue;
    drawItem(&painter, item, QColor(rgb[i % 3], rgb[(i / 3) % 3], rgb[(i / 9) % 3], 128));
  } 
  // Draw selected items on top of unselected items.
  for(int iItem = 0; iItem < iItemCount; ++iItem) 
  {
    QTreeWidgetItem* item = _routeDialog->routeList->topLevelItem(iItem);
    //if(!item)
    if(!item || item->isHidden() || !item->isSelected())
      continue;
    drawItem(&painter, item, Qt::yellow);
    //drawItem(&painter, item, Qt::red);
    //drawItem(&painter, item, QColor(0, 255, 255));
    //drawItem(&painter, item, palette().highlight().color());
  } 
    
    
//     const QColor col(rgb[i % 3], rgb[(i / 3) % 3], rgb[(i / 9) % 3]);
//     if(item->data(RouteDialog::ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && item->data(RouteDialog::ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//     {        
//       const MusECore::Route src = item->data(RouteDialog::ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//       const MusECore::Route dst = item->data(RouteDialog::ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//       QTreeWidgetItem* srcItem = _routeDialog->newSrcList->findItem(src);
//       if(srcItem)
//       {
//         QTreeWidgetItem* dstItem = _routeDialog->newDstList->findItem(dst);
//         if(dstItem)
//         {
//           int src_chan = src.channel;
//           int dst_chan = dst.channel;
//           bool src_wid = false;
//           bool dst_wid = false;
//           switch(src.type)
//           {
//             case MusECore::Route::TRACK_ROUTE:
//               if(src_chan != -1 && src.track && src.track->isMidiTrack())
//               {
//                 for(int i = 0; i < MIDI_CHANNELS; ++i)
//                   if(src_chan & (1 << i))
//                   {
//                     src_chan = i;
//                     break;
//                   }
//               }  // Fall through
//             case MusECore::Route::MIDI_DEVICE_ROUTE:
//             case MusECore::Route::MIDI_PORT_ROUTE:
//               if(src_chan == -1 && src.channels == -1) 
//                 src_wid = true;
//             break;
//             case MusECore::Route::JACK_ROUTE:
//             break;
//           }
//           switch(dst.type)
//           {
//             case MusECore::Route::TRACK_ROUTE:
//               if(dst_chan != -1 && dst.track && dst.track->isMidiTrack())
//               {
//                 for(int i = 0; i < MIDI_CHANNELS; ++i)
//                   if(dst_chan & (1 << i))
//                   {
//                     dst_chan = i;
//                     break;
//                   }
//               } // Fall through
//             case MusECore::Route::MIDI_DEVICE_ROUTE:
//             case MusECore::Route::MIDI_PORT_ROUTE:
//               if(dst_chan == -1 && dst.channels == -1) 
//                 dst_wid = true;
//             break;
//             case MusECore::Route::JACK_ROUTE:
//             break;
//           }
// 
//           if(item == cur_item)
//           {
//             // Remember the selected items and draw that line last over top all else.
//             src_sel = srcItem;
//             dst_sel = dstItem;
//             src_sel_ch = src_chan;
//             dst_sel_ch = dst_chan;
//             sel_wid = src_wid && dst_wid;
//             continue;
//           }
// 
//           if(src_wid && dst_wid) 
//             pen.setWidth(pen_wid_wide);
//           else
//             pen.setWidth(pen_wid_norm);
//           
//           pen.setColor(col);
//           painter.setPen(pen);
//           y1 = itemY(srcItem, true, src_chan) + (yo - yc);
//           y2 = itemY(dstItem, false, dst_chan) + (yi - yc);
//           drawConnectionLine(&painter, x1, y1, x2, y2, h1, h2);
//         }
//         else
//         {
//           DEBUG_PRST_ROUTES(stderr, "ConnectionsView::paintEvent: dstItem not found:\n");
//           src.dump();
//           dst.dump();
//         }
//       }
//       else
//       {
//         DEBUG_PRST_ROUTES(stderr, "ConnectionsView::paintEvent: srcItem not found:\n");
//         src.dump();
//         dst.dump();
//       }
//     }
//   }

//   // Draw the selected items over top all else.
//   if(src_sel && dst_sel)
//   {
//     if(sel_wid) 
//       pen.setWidth(pen_wid_wide);
//     else
//       pen.setWidth(pen_wid_norm);
//     pen.setColor(Qt::yellow);
//     painter.setPen(pen);
//     y1 = itemY(src_sel, true, src_sel_ch) + (yo - yc);
//     y2 = itemY(dst_sel, false, dst_sel_ch) + (yi - yc);
//     drawConnectionLine(&painter, x1, y1, x2, y2, h1, h2);
//   }
}

void ConnectionsView::mousePressEvent(QMouseEvent* e)
{
  e->setAccepted(true);
  lastY = e->y();
}

void ConnectionsView::mouseMoveEvent(QMouseEvent* e)
{
  e->setAccepted(true);
  const Qt::MouseButtons mb = e->buttons();
  const int y = e->y();
  const int ly = lastY;
  lastY = y;
  if(mb & Qt::LeftButton)
     emit scrollBy(0, ly - y);
}

void ConnectionsView::wheelEvent(QWheelEvent* e)
{
  int delta = e->delta();
  DEBUG_PRST_ROUTES(stderr, "ConnectionsView::wheelEvent: delta:%d\n", delta); // REMOVE Tim.
  e->setAccepted(true);
  emit scrollBy(0, delta < 0 ? 1 : -1);
}

// Context menu request event handler.
void ConnectionsView::contextMenuEvent(QContextMenuEvent* /*pContextMenuEvent*/)
{
//         qjackctlConnect *pConnect = m_pConnectView->binding();
//         if (pConnect == 0)
//                 return;
// 
//         QMenu menu(this);
//         QAction *pAction;
// 
//         pAction = menu.addAction(QIcon(":/images/connect1.png"),
//                 tr("&Connect"), pConnect, SLOT(connectSelected()),
//                 tr("Alt+C", "Connect"));
//         pAction->setEnabled(pConnect->canConnectSelected());
//         pAction = menu.addAction(QIcon(":/images/disconnect1.png"),
//                 tr("&Disconnect"), pConnect, SLOT(disconnectSelected()),
//                 tr("Alt+D", "Disconnect"));
//         pAction->setEnabled(pConnect->canDisconnectSelected());
//         pAction = menu.addAction(QIcon(":/images/disconnectall1.png"),
//                 tr("Disconnect &All"), pConnect, SLOT(disconnectAll()),
//                 tr("Alt+A", "Disconect All"));
//         pAction->setEnabled(pConnect->canDisconnectAll());
// 
//         menu.addSeparator();
//         pAction = menu.addAction(QIcon(":/images/refresh1.png"),
//                 tr("&Refresh"), pConnect, SLOT(refresh()),
//                 tr("Alt+R", "Refresh"));
// 
//         menu.exec(pContextMenuEvent->globalPos());
}


// // Widget event slots...
// void ConnectionsView::contentsChanged (void)
// {
//         QWidget::update();
// }


//-----------------------------------
//   RouteTreeWidget
//-----------------------------------

RouteTreeWidget::RouteTreeWidget(QWidget* parent, bool is_input) : QTreeWidget(parent), _isInput(is_input), _channelWrap(false)
{
  if(header())
    connect(header(), SIGNAL(sectionResized(int,int,int)), SLOT(headerSectionResized(int,int,int))); 
}

RouteTreeWidget::~RouteTreeWidget()
{
}

void RouteTreeWidget::computeChannelYValues()
{
  const int ch_w = channelWrap() ? columnWidth(RouteDialog::ROUTE_NAME_COL) : -1;
  QTreeWidgetItemIterator itw(this);
  while(*itw)
  {
    RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(*itw);
//     item->computeChannelYValues(wordWrap() ? viewport()->width() : -1);
    //item->computeChannelYValues(wordWrap() ? columnWidth(RouteDialog::ROUTE_NAME_COL) : -1);
    //item->computeChannelYValues(MusEGlobal::config.routerExpandVertically ? columnWidth(RouteDialog::ROUTE_NAME_COL) : _VERY_LARGE_INTEGER_);
    //item->computeChannelYValues(wordWrap() ? columnWidth(RouteDialog::ROUTE_NAME_COL) : _VERY_LARGE_INTEGER_);
    item->computeChannelYValues(ch_w);
    ++itw;
  }
}

void RouteTreeWidget::headerSectionResized(int logicalIndex, int oldSize, int newSize)
{
   DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::headerSectionResized idx:%d old sz:%d new sz:%d\n", logicalIndex, oldSize, newSize);
//    fprintf(stderr, "RouteTreeWidget::headerSectionResized idx:%d old sz:%d new sz:%d\n", logicalIndex, oldSize, newSize); // REMOVE Tim.
//   scheduleDelayedItemsLayout();

  //if(!wordWrap())
  //  return;
   
  // Self adjust certain item heights...
  // NOTE: Delegate sizeHints are NOT called automatically. scheduleDelayedItemsLayout() seems to solve it. 
  //       But that is costly here! And results in some flickering especially at scrollbar on/off conditions as it fights with itself. 
  //       So check if we really need to do it...
  QTreeWidgetItemIterator ii(this);
  //bool do_layout = false;
  int relayouts = 0;
  while(*ii)
  {
    RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(*ii);
    if(item->testForRelayout(logicalIndex, oldSize, newSize))
    {
      const QModelIndex mdl_idx = indexFromItem(item);
      if(mdl_idx.isValid())
      {
        QAbstractItemDelegate* id = itemDelegate();
        if(RoutingItemDelegate* rid = qobject_cast<RoutingItemDelegate*>(id))
        {
          rid->emitSizeHintChanged(mdl_idx);
          ++relayouts;
        }
      }
    }
    
// //     switch(item->type())
// //     {
// //       case RouteTreeWidgetItem::NormalItem:
// //       break;
// //       
// //       case RouteTreeWidgetItem::CategoryItem:
// //       case RouteTreeWidgetItem::RouteItem:
// //       case RouteTreeWidgetItem::ChannelsItem:
// //       {
//         const QModelIndex midx = indexFromItem(item);
//         if(midx.isValid())
//         {
//           QAbstractItemDelegate* id = itemDelegate();
//           if(RoutingItemDelegate* rid = qobject_cast<RoutingItemDelegate*>(id))
//           {
//             QStyleOptionViewItem vopt;
//             rid->initStyleOption(&vopt, midx);
//     //         if(item->testForRelayout(vopt, midx, oldSize, newSize))
//             //fprintf(stderr, "RouteTreeWidget::headerSectionResized calling rid->testForRelayout\n"); // REMOVE Tim.
//             if(rid->testForRelayout(vopt, midx, oldSize, newSize))
//             {
//               do_layout = true;
// //               item->computeChannelYValues(newSize);
//               //scheduleDelayedItemsLayout();
//               //return;
//             }
//           }
//         }
// //       }
// //       break;
// //     }
    
    ++ii;
  }
  
//   if(do_layout)
//   {
//     //DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::headerSectionResized idx:%d old sz:%d new sz:%d calling scheduleDelayedItemsLayout()\n", logicalIndex, oldSize, newSize);
//     // Neither updateGeometry() or updateGeometries() works here.
//     scheduleDelayedItemsLayout();
//   }
//   if(do_layout)
  if(relayouts)
  {
    //DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::headerSectionResized idx:%d old sz:%d new sz:%d calling scheduleDelayedItemsLayout()\n", logicalIndex, oldSize, newSize);
    //fprintf(stderr, "RouteTreeWidget::headerSectionResized idx:%d old sz:%d new sz:%d no of sizeHintChanged emitted:%d\n", logicalIndex, oldSize, newSize, relayouts); // REMOVE Tim.
    // Neither updateGeometry() or updateGeometries() works here.
//     scheduleDelayedItemsLayout();
    
    // Redraw after computeChannelYValues has been called.
//     update();
    
    //connectionsWidget->update();  // Redraw the connections. FIXME: TODO: Need to access the dialog
  }
  
}

RouteTreeWidgetItem* RouteTreeWidget::itemFromIndex(const QModelIndex& index) const
{
  return static_cast<RouteTreeWidgetItem*>(QTreeWidget::itemFromIndex(index));
}

RouteTreeWidgetItem* RouteTreeWidget::findItem(const MusECore::Route& r, int type)
{
  QTreeWidgetItemIterator ii(this);
  while(*ii)
  {
    QTreeWidgetItem* item = *ii;
    switch(item->type())
    {
      case RouteTreeWidgetItem::NormalItem:
      case RouteTreeWidgetItem::CategoryItem:
      break;
      
      case RouteTreeWidgetItem::RouteItem:
      case RouteTreeWidgetItem::ChannelsItem:
      {
        RouteTreeWidgetItem* rtwi = static_cast<RouteTreeWidgetItem*>(item);
        if((type == -1 || type == item->type()) && rtwi->route().compare(r))
          return rtwi;
      }
      break;
    }
    ++ii;
  }
  return NULL;
  
//   const int cnt = topLevelItemCount(); 
//   for(int i = 0; i < cnt; ++i)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(topLevelItem(i));
//     if(!item)
//       continue;
//     if((type == -1 || type == RouteTreeWidgetItem::CategoryItem) && item->route().compare(r))
//       return item;
// 
//     const int c_cnt = item->childCount();
//     for(int j = 0; j < c_cnt; ++j)
//     {
//       RouteTreeWidgetItem* c_item = static_cast<RouteTreeWidgetItem*>(item->child(j));
//       if(!c_item)
//         continue;
//       if((type == -1 || type == RouteTreeWidgetItem::RouteItem) && c_item->route().compare(r))
//         return c_item;
// 
//       const int cc_cnt = c_item->childCount();
//       for(int k = 0; k < cc_cnt; ++k)
//       {
//         RouteTreeWidgetItem* cc_item = static_cast<RouteTreeWidgetItem*>(c_item->child(k));
//         if(!cc_item)
//           continue;
//         if((type == -1 || type == RouteTreeWidgetItem::ChannelsItem) && cc_item->route().compare(r))
//           return cc_item;
//       }
//     }
//   }
//   return 0;
}

RouteTreeWidgetItem* RouteTreeWidget::findCategoryItem(const QString& name)
{
  const int cnt = topLevelItemCount(); 
  for(int i = 0; i < cnt; ++i)
  {
    RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(topLevelItem(i));
    if(item && item->type() == RouteTreeWidgetItem::CategoryItem && item->text(RouteDialog::ROUTE_NAME_COL) == name)
      return item;
  }
  return 0;
}
      
void RouteTreeWidget::getSelectedRoutes(MusECore::RouteList& routes)
{
  RouteTreeItemList sel = selectedItems();
  const int selSz = sel.size();
  if(selSz == 0)
    return;
  for(int idx = 0; idx < selSz; ++idx)
  {
    RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(sel.at(idx));
    if(!item)
      continue;
    item->getSelectedRoutes(routes);
  }
}

//     if(!item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//       continue;
//     MusECore::Route r = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     if(item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
//     {
//       QBitArray ba = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
//       switch(r.type)
//       {
//         case MusECore::Route::TRACK_ROUTE:
//           if(r.track)
//           {
//             const int sz = ba.size();
//             if(r.track->isMidiTrack())
//             {  
//               for(int i = 0; i < sz; ++i)
//               {
//                 if(i >= MIDI_CHANNELS)
//                   break;
//                 if(ba.testBit(i))
//                 {
//                   r.channel = (1 << i);
//                   routes.push_back(r);
//                 }
//               }
//             }
//             else
//             {
//               for(int i = 0; i < sz; ++i)
//               {
//                 if(ba.testBit(i))
//                 {
//                   r.channel = i;
//                   routes.push_back(r);
//                 }
//               }
//             }
//           }
//         break;
//         case MusECore::Route::JACK_ROUTE:
//         case MusECore::Route::MIDI_DEVICE_ROUTE:
//         case MusECore::Route::MIDI_PORT_ROUTE:
//         break;
//       }
//     }
//     else
//       routes.push_back(r);
//   }
// }

int RouteTreeWidget::channelAt(RouteTreeWidgetItem* item, const QPoint& pt)
{
  const QRect rect = visualItemRect(item);
  
  return item->channelAt(pt, rect);
  
//   QPoint p = pt - rect.topLeft();
// 
//   int w = RouteDialog::midiDotsMargin * 2 + RouteDialog::midiDotDiameter * channels;
//   if(channels > 1)
//     w += RouteDialog::midiDotSpacing * (channels - 1);
//   if(channels > 4)
//     w += RouteDialog::midiDotGroupSpacing * (channels - 1) / 4;
//   
//   const int xoff =_isInput ? rect.width() - w : RouteDialog::midiDotsMargin;
//   const int yoff = RouteDialog::midiDotsMargin + (_isInput ? channels : 0);
//   p.setY(p.y() - yoff);
//   p.setX(p.x() - xoff);
//   if(p.y() < 0 || p.y() >= RouteDialog::midiDotDiameter)
//     return -1;
//   for(int i = 0; i < channels; ++i)
//   {
//     if(p.x() < 0)
//       return -1;
//     if(p.x() < RouteDialog::midiDotDiameter)
//       return i;
//     p.setX(p.x() - RouteDialog::midiDotDiameter - RouteDialog::midiDotSpacing);
//     if(i && ((i % 4) == 0))
//       p.setX(p.x() - RouteDialog::midiDotGroupSpacing);
//   }
//   return -1;
}

void RouteTreeWidget::resizeEvent(QResizeEvent* event)
{
  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::resizeEvent old w:%d h:%d new w:%d h:%d\n", event->oldSize().width(), event->oldSize().height(),
          event->size().width(), event->size().height()); // REMOVE Tim. Persistent routes. Added.

  event->ignore();
  QTreeWidget::resizeEvent(event);
  //if(wordWrap())
  //if(MusEGlobal::config.routerExpandVertically)
//     headerSectionResized(RouteDialog::ROUTE_NAME_COL, event->oldSize().width(), event->size().width()); // ZZZ
}

void RouteTreeWidget::mousePressEvent(QMouseEvent* e)
{
  const QPoint pt = e->pos(); 
  //Qt::KeyboardModifiers km = e->modifiers();
  //bool ctl = km & Qt::ControlModifier;
  //bool shift = km & Qt::ShiftModifier;
  RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(itemAt(pt));
  bool is_cur = item && currentItem() && (item == currentItem());

  //if(is_cur)
  //  QTreeWidget::mousePressEvent(e);
  
  if(item)
  {
    bool changed = item->mousePressHandler(e, visualItemRect(item));
    if(changed)
    {
      //setCurrentItem(item);
      //update(visualItemRect(item));
      QRect r(visualItemRect(item));
      // Need to update from the item's right edge to the viewport right edge,
      //  for the connector lines.
      r.setRight(this->viewport()->geometry().right());
      setDirtyRegion(r);
      //emit itemSelectionChanged();
    }
    
    //if(!is_cur)
      QTreeWidget::mousePressEvent(e);

    if(changed && is_cur)
      //setCurrentItem(item);
      emit itemSelectionChanged();
      
    //e->accept();
    return;
    
  }
  QTreeWidget::mousePressEvent(e);
}    
    
/*    
  if(item && item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
  {        
    const MusECore::Route r = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
    switch(r.type)
    {
      case MusECore::Route::TRACK_ROUTE:
        if(r.track && r.channel != -1 && item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
        {
          int chans;
          if(r.track->isMidiTrack())
            chans = MIDI_CHANNELS;
          else
          {
            MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(r.track);
            if(atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH)
              chans = _isInput ? atrack->totalOutChannels() : atrack->totalInChannels();
            else
              chans = atrack->channels();
          }

          int ch = channelAt(item, pt, chans);
          
          //QBitArray ba = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
          //QBitArray ba_m = ba;
          QBitArray ba_m = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
          const int ba_sz = ba_m.size();
          bool changed = false;
          //if(!ctl)
          {
            //ba_m.fill(false);
            for(int i = 0; i < ba_sz; ++i)
            {
              //const bool b = ba_m.testBit(i);
              
              if(i == ch)
              {
                if(ctl)
                {
                  ba_m.toggleBit(i);
                  changed = true;
                }
                else
                {
                  if(!ba_m.testBit(i))
                    changed = true;
                  ba_m.setBit(i);
                }
              }
              else if(!ctl)
              {
                if(ba_m.testBit(i))
                  changed = true;
                ba_m.clearBit(i);
              }
                
//               //if(ba_m.testBit(i))
//               {
//                 ba_m.clearBit(i);
//                 changed = true;
//               }
            }
          }
//             //clearChannels();
//           //  clearSelection();
//           //int ch = channelAt(item, pt, chans);
//           if(ch != -1 && ch < ba_sz)
//           {
//             ba_m.toggleBit(ch);
//             changed = true;
//           }

          //if(is_cur)
          //  QTreeWidget::mousePressEvent(e);
            
          //if(ba_m != ba)
          if(changed)
          {
            item->setData(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole, qVariantFromValue<QBitArray>(ba_m));
            //setCurrentItem(item);
            update(visualItemRect(item));
            //emit itemSelectionChanged();
          }
          
          //if(!is_cur)
            QTreeWidget::mousePressEvent(e);

          if(changed && is_cur)
            //setCurrentItem(item);
            emit itemSelectionChanged();
            
          //e->accept();
          return;
        }
      break;
      case MusECore::Route::JACK_ROUTE:
      case MusECore::Route::MIDI_DEVICE_ROUTE:
      case MusECore::Route::MIDI_PORT_ROUTE:
      break;
    }
  }  
  QTreeWidget::mousePressEvent(e);
}*/

// REMOVE Tim. Persistent routes. Added.
// void RouteTreeWidget::clearChannels()
// {
//   int cnt = topLevelItemCount(); 
//   for(int i = 0; i < cnt; ++i)
//   {
//     QTreeWidgetItem* item = topLevelItem(i);
//     if(item)
//     {
//       if(item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
//       {
//         QBitArray ba = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
//         ba.fill(false);
//         item->setData(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole, qVariantFromValue<QBitArray>(ba));
//       }
// 
//       int c_cnt = item->childCount();
//       for(int j = 0; j < c_cnt; ++j)
//       {
//         QTreeWidgetItem* c_item = item->child(j);
//         if(c_item)
//         {
//           if(c_item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
//           {
//             QBitArray c_ba = c_item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
//             c_ba.fill(false);
//             c_item->setData(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole, qVariantFromValue<QBitArray>(c_ba));
//           }
// 
//           int cc_cnt = c_item->childCount();
//           for(int k = 0; k < cc_cnt; ++k)
//           {
//             QTreeWidgetItem* cc_item = c_item->child(k);
//             if(cc_item)
//             {
//               if(cc_item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
//               {
//                 //cc_item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>().fill(false);
//                 QBitArray cc_ba = cc_item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
//                 cc_ba.fill(false);
//                 cc_item->setData(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole, qVariantFromValue<QBitArray>(cc_ba));
//               }
//             }
//           }
//         }
//       }
//     }
//   }
// }

void RouteTreeWidget::mouseMoveEvent(QMouseEvent* e)
{
  const QPoint pt = e->pos(); 
  //Qt::KeyboardModifiers km = e->modifiers();
  //bool ctl = km & Qt::ControlModifier;
  //bool shift = km & Qt::ShiftModifier;
  RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(itemAt(pt));
  bool is_cur = item && currentItem() && (item == currentItem());

  //if(is_cur)
  //  QTreeWidget::mouseMoveEvent(e);
  
  if(item)
  {
    bool changed = item->mouseMoveHandler(e, visualItemRect(item));
    if(changed)
    {
      //setCurrentItem(item);
      //update(visualItemRect(item));
      setDirtyRegion(visualItemRect(item));
      //emit itemSelectionChanged();
    }
    
    //if(!is_cur)
      QTreeWidget::mouseMoveEvent(e);

    if(changed && is_cur)
      //setCurrentItem(item);
      emit itemSelectionChanged();
      
    //e->accept();
    return;
    
  }
  QTreeWidget::mouseMoveEvent(e);
}    
    
QItemSelectionModel::SelectionFlags RouteTreeWidget::selectionCommand(const QModelIndex& index, const QEvent* e) const
{
  QItemSelectionModel::SelectionFlags flags = QTreeWidget::selectionCommand(index, e);
  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::selectionCommand flags:%d row:%d col:%d ev type:%d\n", int(flags), index.row(), index.column(), e ? e->type() : -1); // REMOVE Tim. Persistent routes. Added.

  RouteTreeWidgetItem* item = itemFromIndex(index);

  if(item && item->type() == RouteTreeWidgetItem::ChannelsItem)
  {
    if(flags & QItemSelectionModel::Toggle)
    {
      flags &= ~QItemSelectionModel::Toggle;
      flags |= QItemSelectionModel::Select;
      DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::selectionCommand new flags:%d\n", int(flags)); // REMOVE Tim. Persistent routes. Added.
    }
  }
  
  return flags;
}

//   //if(index.data(RouteDialog::RouteRole).canConvert<MusECore::Route>()) 
//   if(item) 
//   {
//     DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::selectionCommand can convert data to Route\n"); // REMOVE Tim. Persistent routes. Added.
//     //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint data is Route\n");  // REMOVE Tim.
//     //const MusECore::Route r = qvariant_cast<MusECore::Route>(index.data(RouteDialog::RouteRole));
//     //const MusECore::Route r = index.data(RouteDialog::RouteRole).value<MusECore::Route>();
//     const MusECore::Route& r = item->route();
//     switch(r.type)
//     {
//       case MusECore::Route::TRACK_ROUTE:
//         //if(e->type() == QEvent:: r.channel != -1)
//         if(r.channel != -1)
//         {
//           if(flags & QItemSelectionModel::Toggle)
//           {
//             flags &= ~QItemSelectionModel::Toggle;
//             flags |= QItemSelectionModel::Select;
//             DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::selectionCommand new flags:%d\n", int(flags)); // REMOVE Tim. Persistent routes. Added.
//           }
//         }
//       break;
//       
//       case MusECore::Route::JACK_ROUTE:
//       case MusECore::Route::MIDI_DEVICE_ROUTE:
//       case MusECore::Route::MIDI_PORT_ROUTE:
//       break;
//     }
//   }
//   return flags;
// }

void RouteTreeWidget::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
  QModelIndexList mil = deselected.indexes();
  const int dsz = mil.size();
  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::selectionChanged: selected size:%d deselected size:%d\n", selected.size(), dsz); // REMOVE Tim. Persistent routes. Added.
  for(int i = 0; i < dsz; ++i)
  {
    const QModelIndex& index = mil.at(i);
    RouteTreeWidgetItem* item = itemFromIndex(index);
    
    if(item && item->type() == RouteTreeWidgetItem::ChannelsItem)
      item->fillSelectedChannels(false);
  }    
  QTreeWidget::selectionChanged(selected, deselected);
}    
//     //if(item && item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//     if(item)
//     {
//       //const MusECore::Route r = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//       const MusECore::Route& r = item->route();
//       switch(r.type)
//       {
//         case MusECore::Route::TRACK_ROUTE:
//           //if(e->type() == QEvent:: r.channel != -1)
//           if(r.channel != -1)
//           {
// //             if(item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
// //             {
// //               DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::selectionChanged: track route: deselected idx:%d clearing channels bitarray\n", i); // REMOVE Tim. Persistent routes. Added.
// //               QBitArray ba = item->data(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
// //               ba.fill(false);
// //               item->setData(RouteDialog::ROUTE_NAME_COL, RouteDialog::ChannelsRole, qVariantFromValue<QBitArray>(ba));
//               item->fillChannels(false);
// //             }
//           }
//         break;
//         
//         case MusECore::Route::JACK_ROUTE:
//         case MusECore::Route::MIDI_DEVICE_ROUTE:
//         case MusECore::Route::MIDI_PORT_ROUTE:
//         break;
//       }
//     }
//     
//   }
//   QTreeWidget::selectionChanged(selected, deselected);
// }

void RouteTreeWidget::scrollBy(int dx, int dy)
{
  DEBUG_PRST_ROUTES(stderr, "RouteTreeWidget::scrollBy: dx:%d dy:%d\n", dx, dy); // REMOVE Tim.
  int hv = horizontalScrollBar()->value();
  int vv = verticalScrollBar()->value();
  if(dx)
  {
    hv += dx;
    horizontalScrollBar()->setValue(hv);
  }
  if(dy)
  {
    vv += dy;
    verticalScrollBar()->setValue(vv);
  }
}

void RouteTreeWidget::getItemsToDelete(QVector<QTreeWidgetItem*>& items_to_remove, bool showAllMidiPorts)
{
  QTreeWidgetItemIterator ii(this);
  while(*ii)
  {
    QTreeWidgetItem* item = *ii;
    if(item)
    {
      QTreeWidgetItem* twi = item;
      while((twi = twi->parent()))
      {
        if(items_to_remove.contains(twi))
          break;
      }
      // No parent found to be deleted. Determine if this should be deleted.
      if(!twi)
      {
        if(!items_to_remove.contains(item))
        {
          RouteTreeWidgetItem* rtwi = static_cast<RouteTreeWidgetItem*>(item);

          switch(rtwi->type())
          {
            case RouteTreeWidgetItem::NormalItem:
            case RouteTreeWidgetItem::CategoryItem:
            case RouteTreeWidgetItem::ChannelsItem:
            break;

            case RouteTreeWidgetItem::RouteItem:
            {
              const MusECore::Route& rt = rtwi->route();
              switch(rt.type)
              {
                case MusECore::Route::MIDI_DEVICE_ROUTE:
                case MusECore::Route::TRACK_ROUTE:
                case MusECore::Route::JACK_ROUTE:
                break;
                
                case MusECore::Route::MIDI_PORT_ROUTE:
                {
                  bool remove_port = false;
                  if(!rt.isValid())
                    remove_port = true;
                  else 
                  if(!showAllMidiPorts)
                  {
                    MusECore::MidiPort* mp = &MusEGlobal::midiPorts[rt.midiPort];
                    if(!mp->device() && (_isInput ? mp->outRoutes()->empty() : mp->inRoutes()->empty()))
                    {
                      
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
                      if(!_isInput)
                      {
                        MusECore::MidiTrackList* tl = MusEGlobal::song->midis();
                        MusECore::ciMidiTrack imt = tl->begin();
                        for( ; imt != tl->end(); ++imt)
                          if((*imt)->outPort() == rt.midiPort)
                            break;
                        if(imt == tl->end())
                          remove_port = true;
                      }
                      else
#endif  // _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
                        remove_port = true;

                    }
                  }

                  if(remove_port)
                    items_to_remove.append(item);
                  ++ii;
                  continue;
                }
                break;
              }
            }
            break;
          }

          if(!rtwi->routeNodeExists())
            items_to_remove.append(item);
        }
      }
    }
    ++ii;
  }
  
/*  
  
  const int cnt = topLevelItemCount(); 
  for(int i = 0; i < cnt; ++i)
  {
    RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(topLevelItem(i));
    if(item)
    {
      const int c_cnt = item->childCount();
      for(int j = 0; j < c_cnt; ++j)
      {
        RouteTreeWidgetItem* c_item = static_cast<RouteTreeWidgetItem*>(item->child(j));
        if(c_item)
        {
          const int cc_cnt = c_item->childCount();
          for(int k = 0; k < cc_cnt; ++k)
          {
            RouteTreeWidgetItem* cc_item = static_cast<RouteTreeWidgetItem*>(c_item->child(k));
            if(cc_item)
            {
              if(!cc_item->routeNodeExists())
                items_to_remove.append(cc_item);
            }
          }
          if(!c_item->routeNodeExists())
            items_to_remove.append(c_item);
        }
      }
      if(!item->routeNodeExists())
        items_to_remove.append(item);
    }
  }*/
}

void RouteTreeWidget::selectRoutes(const QList<QTreeWidgetItem*>& routes, bool doNormalSelections)
{
  QTreeWidgetItemIterator ii(this);
  while(*ii)
  {
    RouteTreeWidgetItem* rtwi = static_cast<RouteTreeWidgetItem*>(*ii);
    switch(rtwi->type())
    {
      case RouteTreeWidgetItem::NormalItem:
      case RouteTreeWidgetItem::CategoryItem:
      case RouteTreeWidgetItem::RouteItem:
      break;
      
      case RouteTreeWidgetItem::ChannelsItem:
      {
        bool do_upd = rtwi->fillChannelsRouteSelected(false);
        if(doNormalSelections && rtwi->fillSelectedChannels(false))
          do_upd = true;
        const MusECore::Route& rtwi_route = rtwi->route();
        const int sz = routes.size();
        for(int i = 0; i < sz; ++i)
        {
          const QTreeWidgetItem* routes_item = routes.at(i);
          const MusECore::Route r = 
            routes_item->data(isInput() ? RouteDialog::ROUTE_SRC_COL : RouteDialog::ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
          if(rtwi_route.compare(r))
          {
            const int chan = r.channel;
            if(chan >= 0)
            {
              //if(!rtwi->channelRouteSelected(chan))
              //{
                rtwi->routeSelectChannel(chan, true);
                do_upd = true;
              //}
              //if(doNormalSelections && !rtwi->channelSelected(chan))
              if(doNormalSelections)
              {
                rtwi->selectChannel(chan, true);
                do_upd = true;
              }
            }

//             const int chans = rtwi->channelCount();
//             for(int c = 0; c < chans; ++c)
//             {
//               // Set both the selected and route selected flags.
//               if(rtwi->channelSelected(c) != (c == chan))
//               {
//                 rtwi->selectChannel(c, c == chan);
//                 do_upd = true;
//               }
//               if(rtwi->channelRouteSelected(c) != (c == chan))
//               {
//                 rtwi->routeSelectChannel(c, c == chan);
//                 do_upd = true;
//               }
//             }
          }
        }
        if(do_upd)
        {
          QRect r(visualItemRect(rtwi));
          // Need to update from the item's right edge to the viewport right edge,
          //  for the connector lines.
          r.setRight(this->viewport()->geometry().right());
          setDirtyRegion(r);
        }
      }
      break;
    }
    ++ii;
  }
}  

//-----------------------------------
//   RoutingItemDelegate
//-----------------------------------

RoutingItemDelegate::RoutingItemDelegate(bool is_input, RouteTreeWidget* tree, QWidget *parent) 
                    : QStyledItemDelegate(parent), _tree(tree), _isInput(is_input)
{
  _firstPress = true;
}

// //-----------------------------------
// //   getItemRectangle
// //   editor is optional and provides info 
// //-----------------------------------
// 
// QRect RoutingItemDelegate::getItemRectangle(const QStyleOptionViewItem& option, const QModelIndex& index, QStyle::SubElement subElement, QWidget* editor) const
// {
//     // Taken from QStyledItemDelegate source. 
//     QStyleOptionViewItemV4 opt = option;
//     initStyleOption(&opt, index);
//     const QWidget* widget = NULL;
//     const QStyleOptionViewItemV3* v3 = qstyleoption_cast<const QStyleOptionViewItemV3*>(&option);
//     if(v3)
//       widget = v3->widget;
//     // Let the editor take up all available space if the editor is not a QLineEdit or it is in a QTableView.
//     #if !defined(QT_NO_TABLEVIEW) && !defined(QT_NO_LINEEDIT)
//     if(editor && qobject_cast<QLineEdit*>(editor) && !qobject_cast<const QTableView*>(widget))
//       opt.showDecorationSelected = editor->style()->styleHint(QStyle::SH_ItemView_ShowDecorationSelected, 0, editor);
//     else
//     #endif
//       opt.showDecorationSelected = true;
//     const QStyle *style = widget ? widget->style() : QApplication::style();
// //     if(editor->layoutDirection() == Qt::RightToLeft)
// //     {
// //       const int delta = qSmartMinSize(editor).width() - r.width();       // qSmartMinSize ???
// //       if (delta > 0)
// //       {
// //         //we need to widen the geometry
// //         r.adjust(-delta, 0, 0, 0);
// //       }
// //     }
// 
//   return style->subElementRect(subElement, &opt, widget);
// }
// 
// //-----------------------------------
// //   subElementHitTest
// //   editor is optional and provides info
// //-----------------------------------
// 
// bool RoutingItemDelegate::subElementHitTest(const QPoint& point, const QStyleOptionViewItem& option, const QModelIndex& index, QStyle::SubElement* subElement, QWidget* editor) const
// {
//   QRect checkBoxRect = getItemRectangle(option, index, QStyle::SE_ItemViewItemCheckIndicator, editor);
//   if(checkBoxRect.isValid() && checkBoxRect.contains(point))
//   {
//     if(subElement)
//       (*subElement) = QStyle::SE_ItemViewItemCheckIndicator;
//     return true;
//   }
// 
//   QRect decorationRect = getItemRectangle(option, index, QStyle::SE_ItemViewItemDecoration, editor);
//   if(decorationRect.isValid() && decorationRect.contains(point))
//   {
//     if(subElement)
//       (*subElement) = QStyle::SE_ItemViewItemDecoration;
//     return true;
//   }
// 
//   QRect textRect = getItemRectangle(option, index, QStyle::SE_ItemViewItemText, editor);
//   if(textRect.isValid() && textRect.contains(point))
//   {
//     if(subElement)
//       (*subElement) = QStyle::SE_ItemViewItemText;
//     return true;
//   }
// 
//   return false;
// }

// //-----------------------------------
// //   updateEditorGeometry
// //-----------------------------------
// 
// void RoutingItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
// {
//   // REMOVE Tim.
//   DEBUG_PRST_ROUTES(stderr, "ColorChooserEditor::updateEditorGeometry editor x:%d y:%d w:%d h:%d rect x:%d y:%d w:%d h:%d\n",
//           editor->x(), editor->y(), editor->width(), editor->height(),
//           option.rect.x(), option.rect.y(), option.rect.width(), option.rect.height());
// 
//   // For the color editor, move it down to the start of the next item so it doesn't cover the current item row.
//   // Width and height are not used - the color editor fixates it's own width and height.
//   if(index.column() == ControlMapperDialog::C_NAME)
//   {
//     QRect r = getItemRectangle(option, index, QStyle::SE_ItemViewItemText, editor);  // Get the text rectangle.
//     if(r.isValid())
//     {
//       editor->move(r.x(), option.rect.y() + option.rect.height());
//       return;
//     }
//   }
//   
//   QStyledItemDelegate::updateEditorGeometry(editor, option, index);
// }

void RoutingItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const
{
//   DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint row:%d col:%d, rect x:%d y:%d w:%d h:%d showDecorationSelected:%d\n",
//           index.row(), index.column(),
//           option.rect.x(), option.rect.y(), option.rect.width(), option.rect.height(),
//           option.showDecorationSelected);  // REMOVE Tim.

  RouteTreeWidgetItem* item = _tree->itemFromIndex(index);
  if(item)
  {
    // Required. option is not automatically filled from index.
    QStyleOptionViewItem vopt(option);
    initStyleOption(&vopt, index);
  
    if(item->paint(painter, vopt, index))
      return;
  }
  QStyledItemDelegate::paint(painter, option, index);
}  

  //QStyleOptionViewItemV4 opt = option;
  //initStyleOption(&opt, index);
  //opt.showDecorationSelected = false;
  
  // TODO: Don't forget these if necessary.
  //painter->save();
  //painter->restore();
  
//     if (index.data().canConvert<StarRating>()) {
//         StarRating starRating = qvariant_cast<StarRating>(index.data());
//
//         if (option.state & QStyle::State_Selected)
//             painter->fillRect(option.rect, option.palette.highlight());
//
//         starRating.paint(painter, option.rect, option.palette,
//                         StarRating::ReadOnly);
//     } else

//   if(index.column() == ControlMapperDialog::C_NAME)
//   {
//     // TODO: Disable all this Style stuff if using a style sheet.
// 
//     //QRect disclosure_r = getItemRectangle(option, index, QStyle::SE_TreeViewDisclosureItem);  // Get the text rectangle.
//     //if(disclosure_r.isValid())
//     //{
//     //}
//       
//     QRect checkbox_r = getItemRectangle(option, index, QStyle::SE_ItemViewItemCheckIndicator);  // Get the text rectangle.
//     if(checkbox_r.isValid())
//     {
//       if(option.state & QStyle::State_Selected)
//         painter->fillRect(checkbox_r & option.rect, option.palette.highlight());
//       QStyleOptionViewItemV4 opt = option;
//       initStyleOption(&opt, index);         // Required ?
//       opt.rect = checkbox_r & option.rect;
//       QApplication::style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &opt, painter);
//       //QApplication::style()->drawControl();
//     }
// 
//     //QApplication::style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, painter);
// 
//     //QApplication::style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &option, painter);
//     
//     QRect deco_r = getItemRectangle(option, index, QStyle::SE_ItemViewItemDecoration);  // Get the text rectangle.
//     if(deco_r.isValid())
//       painter->fillRect(deco_r & option.rect, index.data(Qt::DecorationRole).value<QColor>());
//     
//     QRect text_r = getItemRectangle(option, index, QStyle::SE_ItemViewItemText);  // Get the text rectangle.
//     if(text_r.isValid())
//     {
//       if(option.state & QStyle::State_Selected)
//         painter->fillRect(text_r & option.rect, option.palette.highlight());
//       QApplication::style()->drawItemText(painter, text_r & option.rect, option.displayAlignment, option.palette, true, index.data(Qt::DisplayRole).toString());
//     }
//     
//     return;
//   }
  
  //QStyledItemDelegate::paint(painter, option, index);
  
  //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint\n");  // REMOVE Tim.
  
// //   RouteDialog* router = qobject_cast< RouteDialog* >(parent());
//   //if(parent() && qobject_cast< RouteDialog* >(parent()))
// //   if(router)
// //   {
//     //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint parent is RouteDialog\n");  // REMOVE Tim.
//     //QWidget* qpd = qobject_cast<QWidget*>(painter->device());
//     //if(qpd)
//     if(painter->device())
//     {
//       //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint device is QWidget\n");  // REMOVE Tim.
//       //RouteDialog* router = static_cast<RouteDialog*>(parent());
//       
//       if(index.column() == RouteDialog::ROUTE_NAME_COL && index.data(RouteDialog::RouteRole).canConvert<MusECore::Route>()) 
//       {
//         //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint data is Route\n");  // REMOVE Tim.
//         MusECore::Route r = qvariant_cast<MusECore::Route>(index.data(RouteDialog::RouteRole));
//         QRect rect(option.rect);
//         switch(r.type)
//         {
//           case MusECore::Route::TRACK_ROUTE:
//             //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint route is track\n");  // REMOVE Tim.
//             if(r.track && r.channel != -1)
//             {
//               int chans; 
//               if(r.track->isMidiTrack())
//               {
//                 //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint track is midi\n");  // REMOVE Tim.
//                 chans = MIDI_CHANNELS;
//               }
//               else
//               {
//                 //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint track is audio\n");  // REMOVE Tim.
//                 MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(r.track);
//                 if(atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//                 {
//                   if(_isInput)
//                     chans = atrack->totalOutChannels();
//                   else
//                     chans = atrack->totalInChannels();
//                 }
//                 else
//                   chans = atrack->channels();
//               }
//               
//               int w = RouteDialog::midiDotsMargin * 2 + RouteDialog::midiDotDiameter * chans;
//               if(chans > 1)
//                 w += RouteDialog::midiDotSpacing * (chans - 1);
//               if(chans > 4)
//                 w += RouteDialog::midiDotGroupSpacing * (chans - 1) / 4;
//               
//               //DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::paint src list width:%d src viewport width:%d\n", router->newSrcList->width(), router->newSrcList->viewport()->width());  // REMOVE Tim.
//               //int x = _isInput ? router->newSrcList->viewport()->width() - w : RouteDialog::midiDotsMargin;
//               //int x = _isInput ? painter->device()->width() - w : RouteDialog::midiDotsMargin;
//               int x = _isInput ? _tree->width() - w : RouteDialog::midiDotsMargin;
//               const int y = RouteDialog::midiDotsMargin + (_isInput ? chans : 0);
//               QBitArray ba;
//               int basize = 0;
//               if(index.data(RouteDialog::ChannelsRole).canConvert<QBitArray>())
//               {
//                 ba = index.data(RouteDialog::ChannelsRole).value<QBitArray>();
//                 basize = ba.size();
//               }
//               
//               for(int i = 0; i < chans; )
//               {
//                 painter->setPen(Qt::black);
//                 //painter->drawRoundedRect(option.rect.x() + x, option.rect.y() + y, 
//                 if(!ba.isNull() && i < basize && ba.testBit(i))
//                   painter->fillRect(x, option.rect.y() + y, 
//                                            RouteDialog::midiDotDiameter, RouteDialog::midiDotDiameter,
//                                            option.palette.highlight());
//                 //else
//                   painter->drawRoundedRect(x, option.rect.y() + y, 
//                                            RouteDialog::midiDotDiameter, RouteDialog::midiDotDiameter,
//                                            30, 30);
//                 if((i % 2) == 0)
//                   painter->setPen(Qt::darkGray);
//                 else
//                   painter->setPen(Qt::black);
//                 int xline = x + RouteDialog::midiDotDiameter / 2;
//                 if(_isInput)
//                 {
//                   int yline = option.rect.y() + y;
//                   painter->drawLine(xline, yline, xline, yline - chans + i);
//                   //painter->drawLine(xline, yline - chans + i, painter->device()->width(), yline - chans + i);
//                   painter->drawLine(xline, yline - chans + i, _tree->width(), yline - chans + i);
//                   
//                 }
//                 else
//                 {
//                   int yline = option.rect.y() + RouteDialog::midiDotsMargin + RouteDialog::midiDotDiameter;
//                   painter->drawLine(xline, yline, xline, yline + i);
//                   painter->drawLine(0, yline + i, xline, yline + i);
//                   
//                 }
//                 
//                 ++i;
//                 x += RouteDialog::midiDotDiameter + RouteDialog::midiDotSpacing;
//                 if(i && ((i % 4) == 0))
//                   x += RouteDialog::midiDotGroupSpacing;
//               }
//               return;
//             }
//           break;  
//           case MusECore::Route::MIDI_DEVICE_ROUTE:
//           case MusECore::Route::MIDI_PORT_ROUTE:
//           case MusECore::Route::JACK_ROUTE:
//           break;  
//         }
//       }
//     }
// //   }
//   QStyledItemDelegate::paint(painter, option, index);
// }

// QWidget* RoutingItemDelegate::createEditor(QWidget *parent,
//                                     const QStyleOptionViewItem &option,
//                                     const QModelIndex &index) const
// {
// //     if (index.data().canConvert<StarRating>()) {
// //         StarEditor *editor = new StarEditor(parent);
// //         connect(editor, SIGNAL(editingFinished()),
// //                 this, SLOT(commitAndCloseEditor()));
// //         return editor;
// //     } else
// 
//   int opt_state = option.state;
//   DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::createEditor option state:%d\n", opt_state);  // REMOVE Tim.
// 
//   // HACK: For some reason when using CurrentChanged trigger, createEditor is called upon opening the dialog, yet nothing is selected.
//   // It suddenly started doing that after working just fine. Can't find what may have changed.
//   //if(!(option.state & QStyle::State_Selected))   // Nope. option.state is always the same, never seems to change.
//   //  return NULL;
//   //if(_firstPress)
//   //  return NULL;
//   
//   switch(index.column())
//   {
// //     case ControlMapperDialog::C_SHOW:
// //       //return QStyledItemDelegate::createEditor(parent, option, index);
// //       // This is a checkbox column. No editable info.
// //       //DEBUG_PRST_ROUTES(stderr, "ERROR: RoutingItemDelegate::createEditor called for SHOW column\n");
// //       return 0;
// 
//     //case ControlMapperDialog::C_NAME:
//       //DEBUG_PRST_ROUTES(stderr, "ERROR: RoutingItemDelegate::createEditor called for NAME column\n");
//       // This seems to be a way we can prevent editing of a cell here in this tree widget.
//       // Table widget has individual item cell edting enable but here in tree widget it's per row.
//       //return 0;
// 
//     //case ControlMapperDialog::C_COLOR:
//     case ControlMapperDialog::C_NAME:
//     {
//       ColorEditor* color_list = new ColorEditor(parent);
//       //connect(color_list, SIGNAL(activated(int)), this, SLOT(colorEditorChanged()));
//       connect(color_list, SIGNAL(activated(const QColor&)), this, SLOT(editorChanged()));
//       return color_list;
//     }
// 
//     case ControlMapperDialog::C_ASSIGN_PORT:
//     {
//       QComboBox* combo = new QComboBox(parent);
// 
// //       combo->addItem(tr("<None>"), -1);
// //       combo->addItem(tr("Control7"), MusECore::MidiController::Controller7);
// //       combo->addItem(tr("Control14"), MusECore::MidiController::Controller14);
// //       combo->addItem(tr("RPN"), MusECore::MidiController::RPN);
// //       combo->addItem(tr("NPRN"), MusECore::MidiController::NRPN);
// //       combo->addItem(tr("RPN14"), MusECore::MidiController::RPN14);
// //       combo->addItem(tr("NRPN14"), MusECore::MidiController::NRPN14);
// //       combo->addItem(tr("Pitch"), MusECore::MidiController::Pitch);
// //       combo->addItem(tr("Program"), MusECore::MidiController::Program);
// //       //combo->addItem(tr("PolyAftertouch"), MusECore::MidiController::PolyAftertouch); // Not supported yet. Need a way to select pitch.
// //       combo->addItem(tr("Aftertouch"), MusECore::MidiController::Aftertouch);
// //       //combo->setCurrentIndex(0);
// 
// //       combo->addItem(tr("<None>"), -1);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Controller7), MusECore::MidiController::Controller7);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Controller14), MusECore::MidiController::Controller14);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::RPN), MusECore::MidiController::RPN);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::NRPN), MusECore::MidiController::NRPN);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::RPN14), MusECore::MidiController::RPN14);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::NRPN14), MusECore::MidiController::NRPN14);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Pitch), MusECore::MidiController::Pitch);
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Program), MusECore::MidiController::Program);
// //       //combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::PolyAftertouch), MusECore::MidiController::PolyAftertouch); // Not supported yet. Need a way to select pitch.
// //       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Aftertouch), MusECore::MidiController::Aftertouch);
// 
//       combo->addItem("---", -1);
//       int port = index.data(RouteDialog::RouteRole).toInt();
//       QString port_name;
//       for(int i = 0; i < MIDI_PORTS; ++i)
//       {
//         MusECore::MidiDevice* md = MusEGlobal::midiPorts[i].device();
//         //if(!md)  // In the case of this combo box, don't bother listing empty ports.
//         //  continue;
//         //if(!(md->rwFlags() & 1 || md->isSynti()) && (i != outPort))
//         if(!(md && (md->rwFlags() & 2)) && (i != port))   // Only readable ports, or current one.
//           continue;
//         //name.sprintf("%d:%s", i+1, MusEGlobal::midiPorts[i].portname().toLatin1().constData());
//         QString name = QString("%1:%2").arg(i+1).arg(MusEGlobal::midiPorts[i].portname());
//         combo->addItem(name, i);
//       }
//       connect(combo, SIGNAL(currentIndexChanged(int)), this, SLOT(editorChanged()));
//       return combo;
//     }
// 
//     case ControlMapperDialog::C_ASSIGN_CHAN:
//     {
// //       QSpinBox* spin_box = new QSpinBox(parent);
// //       spin_box->setMinimum(0);
// //       spin_box->setMaximum(127);
// //       return spin_box;
// 
//       QWidget* widget = QStyledItemDelegate::createEditor(parent, option, index);
//       QSpinBox* spin_box = qobject_cast<QSpinBox*>(widget);
//       if(spin_box)
//       {
//         spin_box->setMinimum(0);
//         spin_box->setMaximum(MIDI_CHANNELS - 1);
//       }
//       return widget;
//     }
//     
//     case ControlMapperDialog::C_MCTL_NUM:
//     {
//       QComboBox* combo = new QComboBox(parent);
// 
// //       combo->addItem(tr("<None>"), -1);
// //       combo->addItem(tr("Control7"), MusECore::MidiController::Controller7);
// //       combo->addItem(tr("Control14"), MusECore::MidiController::Controller14);
// //       combo->addItem(tr("RPN"), MusECore::MidiController::RPN);
// //       combo->addItem(tr("NPRN"), MusECore::MidiController::NRPN);
// //       combo->addItem(tr("RPN14"), MusECore::MidiController::RPN14);
// //       combo->addItem(tr("NRPN14"), MusECore::MidiController::NRPN14);
// //       combo->addItem(tr("Pitch"), MusECore::MidiController::Pitch);
// //       combo->addItem(tr("Program"), MusECore::MidiController::Program);
// //       //combo->addItem(tr("PolyAftertouch"), MusECore::MidiController::PolyAftertouch); // Not supported yet. Need a way to select pitch.
// //       combo->addItem(tr("Aftertouch"), MusECore::MidiController::Aftertouch);
// //       //combo->setCurrentIndex(0);
// 
//       //combo->addItem(tr("<None>"), -1);
//       combo->addItem("---", -1);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Controller7), MusECore::MidiController::Controller7);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Controller14), MusECore::MidiController::Controller14);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::RPN), MusECore::MidiController::RPN);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::NRPN), MusECore::MidiController::NRPN);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::RPN14), MusECore::MidiController::RPN14);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::NRPN14), MusECore::MidiController::NRPN14);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Pitch), MusECore::MidiController::Pitch);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Program), MusECore::MidiController::Program);
//       // TODO Per-pitch controls not supported yet. Need a way to select pitch.
//       //combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::PolyAftertouch), MusECore::MidiController::PolyAftertouch);
//       combo->addItem(MusECore::int2ctrlType(MusECore::MidiController::Aftertouch), MusECore::MidiController::Aftertouch);
//       connect(combo, SIGNAL(currentIndexChanged(int)), this, SLOT(editorChanged()));
//       return combo;
//     }
// 
// //     case ControlMapperDialog::C_MCTL_H:
// //     {
// // //       QSpinBox* spin_box = new QSpinBox(parent);
// // //       spin_box->setMinimum(0);
// // //       spin_box->setMaximum(127);
// // //       return spin_box;
// //       
// //       QWidget* widget = QStyledItemDelegate::createEditor(parent, option, index);
// //       QSpinBox* spin_box = qobject_cast<QSpinBox*>(widget);
// //       if(spin_box)
// //       {
// //         spin_box->setMinimum(0);
// //         spin_box->setMaximum(127);
// //       }
// //       return widget;
// //     }
// 
// ///     case ControlMapperDialog::C_MCTL_H:
// //     case ControlMapperDialog::C_MCTL_L:
// //     {
// // //       QSpinBox* spin_box = new QSpinBox(parent);
// // //       spin_box->setMinimum(0);
// // //       spin_box->setMaximum(127);
// // //       return spin_box;
// //       
// //       QWidget* widget = QStyledItemDelegate::createEditor(parent, option, index);
// //       QSpinBox* spin_box = qobject_cast<QSpinBox*>(widget);
// //       if(spin_box)
// //       {
// //         spin_box->setMinimum(0);
// //         spin_box->setMaximum(127);
// //       }
// //       return widget;
// //     }
//   }
//   
//   return QStyledItemDelegate::createEditor(parent, option, index);
// }

// void RoutingItemDelegate::editorChanged()
// {
// //     StarEditor *editor = qobject_cast<StarEditor *>(sender());
// //     emit commitData(editor);
// //     emit closeEditor(editor);
// 
//   DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::editorChanged\n");  // REMOVE Tim.
//   
//   // Wow, I thought using sender was frowned upon ("breaks modularity"). But hey, it's necessary sometimes. TODO Improve this?
//   //ColorEditor* editor = qobject_cast<ColorEditor*>(sender());
//   QWidget* editor = qobject_cast<QWidget*>(sender());
//   if(editor)
//   {
//     emit commitData(editor);
//     emit closeEditor(editor);
//   }
// }

// // void RoutingItemDelegate::commitAndCloseEditor()
// // {
// // //     StarEditor *editor = qobject_cast<StarEditor *>(sender());
// // //     emit commitData(editor);
// // //     emit closeEditor(editor);
// // }

// void RoutingItemDelegate::setEditorData(QWidget *editor,
//                                   const QModelIndex &index) const
// {
//   DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::setEditorData\n");  // REMOVE Tim.
// //      if (index.data().canConvert<StarRating>()) {
// //          StarRating starRating = qvariant_cast<StarRating>(index.data());
// //          StarEditor *starEditor = qobject_cast<StarEditor *>(editor);
// //          starEditor->setStarRating(starRating);
// //      } else
//   
//    //if(index.column() == ControlMapperDialog::C_COLOR)
// 
// 
//   switch(index.column())
//   {
//     case ControlMapperDialog::C_NAME:
//     {
//       ColorEditor* color_editor = qobject_cast<ColorEditor*>(editor);
//       if(color_editor)
//         color_editor->setColor(index.data(Qt::DecorationRole).value<QColor>());
//       return;
//     }
// 
//     case ControlMapperDialog::C_ASSIGN_PORT:
//     case ControlMapperDialog::C_MCTL_NUM:
//     {
//       QComboBox* combo = qobject_cast<QComboBox*>(editor);
//       if(combo)
//       {
//         int data = index.data(RouteDialog::RouteRole).toInt();
//         int idx = combo->findData(data);
//         if(idx != -1)
//         {
//           combo->blockSignals(true);     // Prevent currentIndexChanged or activated from being called
//           combo->setCurrentIndex(idx);
//           combo->blockSignals(false);
//         }
//       }
//       return;
//     }
// 
//     default:
//       QStyledItemDelegate::setEditorData(editor, index);
//   }
//    
// //    if(index.column() == ControlMapperDialog::C_NAME)
// //    {
// //      ColorEditor* color_editor = qobject_cast<ColorEditor*>(editor);
// //      if(color_editor)
// //        color_editor->setColor(index.data(Qt::DecorationRole).value<QColor>());
// //    }
// //    else
// //    if(index.column() == ControlMapperDialog::C_ASSIGN_PORT)
// //    {
// //      QComboBox* combo = qobject_cast<QComboBox*>(editor);
// //      if(combo)
// //      {
// //        int data = index.data(RouteDialog::RouteRole).toInt();
// //        int idx = combo->findData(data);
// //        if(idx != -1)
// //          combo->setCurrentIndex(idx);
// //      }
// //    }
// //    else
// //    if(index.column() == ControlMapperDialog::C_MCTL_TYPE)
// //    {
// //      QComboBox* combo = qobject_cast<QComboBox*>(editor);
// //      if(combo)
// //      {
// //        int data = index.data(RouteDialog::RouteRole).toInt();
// //        int idx = combo->findData(data);
// //        if(idx != -1)
// //          combo->setCurrentIndex(idx);
// //      }
// //    }
// //    else
// //      QStyledItemDelegate::setEditorData(editor, index);
// }

void RoutingItemDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                 const QModelIndex &index) const
{
  DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::setModelData\n");  // REMOVE Tim.
//      if (index.data().canConvert<StarRating>()) {
//          StarEditor *starEditor = qobject_cast<StarEditor *>(editor);
//          model->setData(index, QVariant::fromValue(starEditor->starRating()));
//      } else

   //if(index.column() == ControlMapperDialog::C_COLOR)

  switch(index.column())
  {
//     case ControlMapperDialog::C_NAME:
//     {
//       ColorEditor* color_editor = qobject_cast<ColorEditor*>(editor);
//       if(color_editor)
//         model->setData(index, color_editor->color(), Qt::DecorationRole);
//       return;
//     }
// 
//     case ControlMapperDialog::C_ASSIGN_PORT:
//     case ControlMapperDialog::C_MCTL_NUM:
//     {
//       QComboBox* combo = qobject_cast<QComboBox*>(editor);
//       if(combo)
//       {
//         int idx = combo->currentIndex();
//         if(idx != -1)
//         {
//           model->setData(index, combo->itemData(idx), RouteDialog::RouteRole);    // Do this one before the text so that the tree view's itemChanged handler gets it first!
//           model->blockSignals(true);
//           model->setData(index, combo->itemText(idx), Qt::DisplayRole); // This will cause another handler call. Prevent it by blocking.
//           model->blockSignals(false);
//         }
//       }
//       return;
//     }

    default:
       QStyledItemDelegate::setModelData(editor, model, index);
  }

//    if(index.column() == ControlMapperDialog::C_NAME)
//    {
//      ColorEditor* color_editor = qobject_cast<ColorEditor*>(editor);
//      if(color_editor)
//        model->setData(index, color_editor->color(), Qt::DecorationRole);
//    }
//    else
//    if(index.column() == ControlMapperDialog::C_ASSIGN_PORT)
//    {
//      QComboBox* combo = qobject_cast<QComboBox*>(editor);
//      if(combo)
//      {
//        int idx = combo->currentIndex();
//        if(idx != -1)
//        {
//          model->setData(index, combo->itemText(idx), Qt::DisplayRole);
//          model->setData(index, combo->itemData(idx), RouteDialog::RouteRole);
//        }
//      }
//    }
//    else
//    if(index.column() == ControlMapperDialog::C_MCTL_TYPE)
//    {
//      QComboBox* combo = qobject_cast<QComboBox*>(editor);
//      if(combo)
//      {
//        int idx = combo->currentIndex();
//        if(idx != -1)
//        {
//          model->setData(index, combo->itemText(idx), Qt::DisplayRole);
//          model->setData(index, combo->itemData(idx), RouteDialog::RouteRole);
//        }
//      }
//    }
//    else
//      QStyledItemDelegate::setModelData(editor, model, index);
}

QSize RoutingItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
//     if (index.data().canConvert<StarRating>()) {
//         StarRating starRating = qvariant_cast<StarRating>(index.data());
//         return starRating.sizeHint();
//     } else

//   if(index.column() == ControlMapperDialog::C_COLOR)
//     return QSize(__COLOR_CHOOSER_ELEMENT_WIDTH__ * __COLOR_CHOOSER_NUM_COLUMNS__,
//                  __COLOR_CHOOSER_ELEMENT_HEIGHT__ * (__COLOR_CHOOSER_NUM_ELEMENTS__ / __COLOR_CHOOSER_NUM_COLUMNS__));
//     
  //return QStyledItemDelegate::sizeHint(option, index);

  DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::sizeHint\n"); // REMOVE Tim.
  
  if(RouteTreeWidgetItem* item = _tree->itemFromIndex(index))
  {
//     // Required. option is not automatically filled from index.
//     QStyleOptionViewItem vopt(option);
//     initStyleOption(&vopt, index);
    
//     const QSize sz = item->getSizeHint(vopt, index);
//     const QSize sz = item->getSizeHint(index.column(), _tree->wordWrap() ? _tree->viewport()->width() : -1); 
    //const QSize sz = item->getSizeHint(index.column(), _tree->wordWrap() ? _tree->columnWidth(RouteDialog::ROUTE_NAME_COL) : _VERY_LARGE_INTEGER_); 
//     const QSize sz = item->getSizeHint(index.column(), _tree->wordWrap() ? _tree->columnWidth(RouteDialog::ROUTE_NAME_COL) : -1); 
    const QSize sz = item->getSizeHint(index.column(), _tree->columnWidth(RouteDialog::ROUTE_NAME_COL)); 
    if(sz.isValid())
    {
      //fprintf(stderr, "RoutingItemDelegate::sizeHint w:%d h:%d\n", sz.width(), sz.height()); // REMOVE Tim.
      return sz;
    }
  }
  return QStyledItemDelegate::sizeHint(option, index);
}  

//   RouteDialog* router = qobject_cast< RouteDialog* >(parent());
//   if(router)
//   {
//     if(index.column() == RouteDialog::ROUTE_NAME_COL && index.data(RouteDialog::RouteRole).canConvert<MusECore::Route>()) 
//     {
//       MusECore::Route r = qvariant_cast<MusECore::Route>(index.data(RouteDialog::RouteRole));
//       switch(r.type)
//       {
//         case MusECore::Route::TRACK_ROUTE:
//           if(r.track && r.channel != -1)
//           {
//             int chans; 
//             if(r.track->isMidiTrack())
//               chans = MIDI_CHANNELS;
//             else
//             {
//               MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(r.track);
//               if(atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//               {
//                 if(_isInput)
//                   chans = atrack->totalOutChannels();
//                 else
//                   chans = atrack->totalInChannels();
//               }
//               else
//                 chans = atrack->channels();
//             }
//             int w = RouteDialog::midiDotsMargin * 2 + RouteDialog::midiDotDiameter * chans;
//             if(chans > 1)
//               w += RouteDialog::midiDotSpacing * (chans - 1);
//             if(chans > 4)
//               w += RouteDialog::midiDotGroupSpacing * (chans - 1) / 4;
//             const int h = RouteDialog::midiDotDiameter + RouteDialog::midiDotsMargin * 2 + chans;
//             return QSize(w, h);
//           }
//         break;  
//         case MusECore::Route::MIDI_DEVICE_ROUTE:
//         case MusECore::Route::MIDI_PORT_ROUTE:
//         case MusECore::Route::JACK_ROUTE:
//         break;  
//       }
//     }
//   }
//   return QStyledItemDelegate::sizeHint(option, index);
// }

// bool RoutingItemDelegate::testForRelayout(const QStyleOptionViewItem &option, const QModelIndex& index, int old_width, int new_width) 
// {
//   if(index.column() == RouteDialog::ROUTE_NAME_COL)
//   {
//     //index.data(RouteTreeWidgetItem::TypeRole);
//     
//     QStyleOptionViewItem vopt(option);
//     
//     vopt.rect = QRect(vopt.rect.x(), vopt.rect.y(), old_width, -1);
//     const QSize old_sz = sizeHint(vopt, index);
//     vopt.rect = QRect(vopt.rect.x(), vopt.rect.y(), new_width, -1);
//     const QSize new_sz = sizeHint(vopt, index);
//     
//     //return old_sz.isValid() && new_sz.isValid() && old_sz.height() != new_sz.height();
//     //return old_sz.isValid() && new_sz.isValid() && old_sz != new_sz;
//     
//     //if(old_sz != new_sz)
//     if(old_sz.height() != new_sz.height())
//     {
// //       emit sizeHintChanged(index);
//       return true;
//     }
//   }
//   return false;
// }



bool RoutingItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
//   if(event->type() == QEvent::MouseMove)
//   {
//     QMouseEvent* me = static_cast<QMouseEvent*>(event);
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::editorEvent: Move X:%d Y:%d gX:%d gY:%d\n", me->x(), me->y(), me->globalX(), me->globalY());  // REMOVE Tim.
//     // If any buttons down, ignore.
// //     if(me->buttons() != Qt::NoButton)
// //     {
// //       event->accept();
// //       return true;
// //     }
//   }
//   else
//   if(event->type() == QEvent::MouseButtonPress)
//   {
//     QMouseEvent* me = static_cast<QMouseEvent*>(event);
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::editorEvent: Press X:%d Y:%d gX:%d gY:%d\n", me->x(), me->y(), me->globalX(), me->globalY());  // REMOVE Tim.
// 
// //     _firstPress = false;  // HACK
// //     
// //     QStyle::SubElement sub_element;
// //     if(subElementHitTest(me->pos(), option, index, &sub_element))
// //       _currentSubElement = sub_element;
// //     //event->accept();
// //     //return true;
//   }
//   else
//   if(event->type() == QEvent::MouseButtonRelease)
//   {
//     QMouseEvent* me = static_cast<QMouseEvent*>(event);
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::editorEvent: Release X:%d Y:%d gX:%d gY:%d\n", me->x(), me->y(), me->globalX(), me->globalY());  // REMOVE Tim.
// 
// //     // If the element under the mouse is not the one when pressed, eat up these events because
// //     //  they trigger the editor or action of the element under the mouse at the release position.
// //     QStyle::SubElement sub_element = _currentSubElement;
// //     if(!subElementHitTest(me->pos(), option, index, &sub_element) || sub_element != _currentSubElement)
// //     //QRect r = getItemRectangle(option, index, QStyle::SE_ItemViewItemDecoration);
// //     //if(!subElementHitTest(me->pos(), option, index, &sub_element) ||
// //     //  (sub_element != QStyle::SE_ItemViewItemCheckIndicator && sub_element != QStyle::SE_ItemViewItemDecoration))
// //     //if(r.isValid())
// //     {
// //       event->accept();
// //       return true;
// //     }
//   }
//   else
//   if(event->type() == QEvent::Close)
//   {
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::editorEvent: Close\n");  // REMOVE Tim.
//   }
//   else
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::editorEvent: event type:%d\n", event->type());  // REMOVE Tim.


//   switch(index.column())
//   {
//     case ControlMapperDialog::C_SHOW:
//       // This is checkbox column. No editable info.
//       //event->accept();
//       //return true;
//       //return false;
//       return QStyledItemDelegate::editorEvent(event, model, option, index);
// 
//     case ControlMapperDialog::C_NAME:
//       // This is non-editable name.
//       event->accept();
//       return true;
// 
//     case ControlMapperDialog::C_COLOR:
//     {
//       if(event->type() == QEvent::MouseButtonRelease)
//       {
//         QMouseEvent* me = static_cast<QMouseEvent*>(event);
//         DEBUG_PRST_ROUTES(stderr, " X:%d Y:%d gX:%d gY:%d\n", me->x(), me->y(), me->globalX(), me->globalY());  // REMOVE Tim.
// 
//       }
// 
//       event->accept();
//       return true;
//     }
// 
//     case ControlMapperDialog::C_ASSIGN:
//       // This is editable assigned input controller.
//       return false;
// 
//     case ControlMapperDialog::C_MCTL_TYPE:
//       // This is editable midi control type.
//       return false;
// 
//     case ControlMapperDialog::C_MCTL_H:
//       // This is editable midi control num high.
//       return false;
// 
//     case ControlMapperDialog::C_MCTL_L:
//       // This is editable midi control num low.
//       return false;
//   }
// 
//   return false;

  return QStyledItemDelegate::editorEvent(event, model, option, index);
}


bool RoutingItemDelegate::eventFilter(QObject* editor, QEvent* event)
{
//   if(event->type() == QEvent::MouseButtonPress)
//   {
//     QMouseEvent* me = static_cast<QMouseEvent*>(event);
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::eventFilter: Press X:%d Y:%d gX:%d gY:%d\n", me->x(), me->y(), me->globalX(), me->globalY());  // REMOVE Tim.
//     //event->accept();
//     //return true;
//   }
//   else
//   if(event->type() == QEvent::MouseButtonRelease)
//   {
//     QMouseEvent* me = static_cast<QMouseEvent*>(event);
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::eventFilter: Release X:%d Y:%d gX:%d gY:%d\n", me->x(), me->y(), me->globalX(), me->globalY());  // REMOVE Tim.
//     //event->accept();
//     //return true;
//   }
//   else
//   if(event->type() == QEvent::Close)
//   {
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::eventFilter: Close\n");  // REMOVE Tim.
//   }
//   else
//     DEBUG_PRST_ROUTES(stderr, "RoutingItemDelegate::eventFilter: event type:%d\n", event->type());  // REMOVE Tim.

  return QStyledItemDelegate::eventFilter(editor, event);
}





//---------------------------------------------------------
//   RouteDialog
//---------------------------------------------------------

RouteDialog::RouteDialog(QWidget* parent)
   : QDialog(parent)
{
  setupUi(this);

//   newSrcList->setWordWrap(false);
//   newDstList->setWordWrap(false);
//   routeList->setWordWrap(false);
//   newSrcList->setTextElideMode(Qt::ElideNone);
//   newDstList->setTextElideMode(Qt::ElideNone);
//   routeList->setTextElideMode(Qt::ElideNone);
  
  filterSrcButton->setIcon(*routerFilterSourceIcon);
  filterDstButton->setIcon(*routerFilterDestinationIcon);
  srcRoutesButton->setIcon(*routerFilterSourceRoutesIcon);
  dstRoutesButton->setIcon(*routerFilterDestinationRoutesIcon);
  allMidiPortsButton->setIcon(*settings_midiport_softsynthsIcon);
  verticalLayoutButton->setIcon(*routerViewSplitterIcon);
  
  routeAliasList->addItem(tr("Normal"), QVariant::fromValue<int>(MusEGlobal::RoutePreferCanonicalName));
  routeAliasList->addItem(tr("Alias 1"), QVariant::fromValue<int>(MusEGlobal::RoutePreferFirstAlias));
  routeAliasList->addItem(tr("Alias 2"), QVariant::fromValue<int>(MusEGlobal::RoutePreferSecondAlias));

//   verticalLayoutButton->setChecked(newSrcList->wordWrap() || newDstList->wordWrap());
  
  //newSrcList->viewport()->setLayoutDirection(Qt::LeftToRight);
  
  //_srcFilterItem = NULL;
  //_dstFilterItem = NULL;

  srcItemDelegate = new RoutingItemDelegate(true, newSrcList, this);
  dstItemDelegate = new RoutingItemDelegate(false, newDstList, this);
  
  newSrcList->setItemDelegate(srcItemDelegate);
  newDstList->setItemDelegate(dstItemDelegate);
  
  //newSrcList->setItemsExpandable(false);  // REMOVE Tim. For test only.
  //newDstList->setItemsExpandable(false);  // REMOVE Tim. For test only.
  
  connectionsWidget->setRouteDialog(this);

  // REMOVE Tim. Persistent routes. Added, changed.
  QStringList columnnames;
  columnnames << tr("Source");
              //<< tr("Type");
  newSrcList->setColumnCount(columnnames.size());
  newSrcList->setHeaderLabels(columnnames);
  for (int i = 0; i < columnnames.size(); ++i) {
        //setWhatsThis(newSrcList->horizontalHeaderItem(i), i);
        //setToolTip(newSrcList->horizontalHeaderItem(i), i);
        }
        
  columnnames.clear();
  columnnames << tr("Destination");
              //<< tr("Type");
  newDstList->setColumnCount(columnnames.size());
  newDstList->setHeaderLabels(columnnames);
  for (int i = 0; i < columnnames.size(); ++i) {
        //setWhatsThis(newDstList->horizontalHeaderItem(i), i);
        //setToolTip(newDstList->horizontalHeaderItem(i), i);
        }

  // We are using right-to-left layout for the source tree widget, to force the scroll bar on the left.
  // But this makes incorrect tree indentation (it indents towards the LEFT).
  // And for both the source and destination tree widgets the tree indent marks interfere with the column contents placement (pushing it over).
  // In this case, the tree works better (best) when a second column is reserved for it (LEFT of the source column, RIGHT of destination column.)
  // But that makes an awkward left-indenting tree left of the source column and right-indenting tree right of the destination column.
  // So get rid of the tree, move it to a second column, which does not exist. We will draw and handle our own tree.
  newSrcList->setTreePosition(1);
  newDstList->setTreePosition(1);
  
  // Need this. Don't remove.
//   newSrcList->header()->setSectionResizeMode(QHeaderView::Stretch);
//   newDstList->header()->setSectionResizeMode(QHeaderView::Stretch);
//   newSrcList->header()->setSectionResizeMode(QHeaderView::Interactive);
//   newDstList->header()->setSectionResizeMode(QHeaderView::Interactive);

  //newSrcList->header()->setStretchLastSection(false);
  //newDstList->header()->setStretchLastSection(false);
  
  newSrcList->setTextElideMode(Qt::ElideMiddle);
  newDstList->setTextElideMode(Qt::ElideMiddle);

  
  columnnames.clear();
  columnnames << tr("Source")
              << tr("Destination");
  routeList->setColumnCount(columnnames.size());
  routeList->setHeaderLabels(columnnames);
  for (int i = 0; i < columnnames.size(); ++i) {
        //setWhatsThis(routeList->horizontalHeaderItem(i), i);
        //setToolTip(routeList->horizontalHeaderItem(i), i);
        }
  
  // Make it so that the column(s) cannot be shrunk below the size of one group of channels in a ChannelsItem.
  newSrcList->header()->setMinimumSectionSize(RouteChannelsList::minimumWidthHint());
  newDstList->header()->setMinimumSectionSize(RouteChannelsList::minimumWidthHint());
  
  verticalLayoutButton->setChecked(MusEGlobal::config.routerExpandVertically);
  if(MusEGlobal::config.routerExpandVertically)
  {
//     newSrcList->resizeColumnToContents(ROUTE_NAME_COL);
//     newDstList->resizeColumnToContents(ROUTE_NAME_COL);
    newSrcList->setWordWrap(true);
    newDstList->setWordWrap(true);
    newSrcList->setChannelWrap(true);
    newDstList->setChannelWrap(true);
    newSrcList->header()->setSectionResizeMode(QHeaderView::Stretch);
    newDstList->header()->setSectionResizeMode(QHeaderView::Stretch);
    newSrcList->setColumnWidth(ROUTE_NAME_COL, RouteChannelsList::minimumWidthHint());
    newDstList->setColumnWidth(ROUTE_NAME_COL, RouteChannelsList::minimumWidthHint());
  }
  else
  {
    newSrcList->setWordWrap(false);
    newDstList->setWordWrap(false);
    newSrcList->setChannelWrap(true);
    newDstList->setChannelWrap(true);
    newSrcList->header()->setSectionResizeMode(QHeaderView::Interactive);
    newDstList->header()->setSectionResizeMode(QHeaderView::Interactive);
  }
  
  songChanged(SC_EVERYTHING);

  connect(newSrcList->verticalScrollBar(), SIGNAL(rangeChanged(int,int)), srcTreeScrollBar, SLOT(setRange(int,int))); 
  connect(newDstList->verticalScrollBar(), SIGNAL(rangeChanged(int,int)), dstTreeScrollBar, SLOT(setRange(int,int))); 
  connect(newSrcList->verticalScrollBar(), SIGNAL(valueChanged(int)), SLOT(srcTreeScrollValueChanged(int))); 
  connect(newDstList->verticalScrollBar(), SIGNAL(valueChanged(int)), SLOT(dstTreeScrollValueChanged(int))); 
  connect(srcTreeScrollBar, SIGNAL(valueChanged(int)), SLOT(srcScrollBarValueChanged(int))); 
  connect(dstTreeScrollBar, SIGNAL(valueChanged(int)), SLOT(dstScrollBarValueChanged(int))); 
  
  connect(routeList, SIGNAL(itemSelectionChanged()), SLOT(routeSelectionChanged()));
  connect(newSrcList, SIGNAL(itemSelectionChanged()), SLOT(srcSelectionChanged()));
  connect(newDstList, SIGNAL(itemSelectionChanged()), SLOT(dstSelectionChanged()));
  //connect(newSrcList->verticalScrollBar(), SIGNAL(sliderMoved(int)), connectionsWidget, SLOT(update()));
  //connect(newDstList->verticalScrollBar(), SIGNAL(sliderMoved(int)), connectionsWidget, SLOT(update()));
  connect(newSrcList->verticalScrollBar(), SIGNAL(valueChanged(int)), connectionsWidget, SLOT(update()));
  connect(newDstList->verticalScrollBar(), SIGNAL(valueChanged(int)), connectionsWidget, SLOT(update()));
  connect(newSrcList, SIGNAL(itemCollapsed(QTreeWidgetItem*)), connectionsWidget, SLOT(update()));
  connect(newSrcList, SIGNAL(itemExpanded(QTreeWidgetItem*)), connectionsWidget, SLOT(update()));
  connect(newDstList, SIGNAL(itemCollapsed(QTreeWidgetItem*)), connectionsWidget, SLOT(update()));
  connect(newDstList, SIGNAL(itemExpanded(QTreeWidgetItem*)), connectionsWidget, SLOT(update()));
  connect(connectionsWidget, SIGNAL(scrollBy(int, int)), newSrcList, SLOT(scrollBy(int, int)));
  connect(connectionsWidget, SIGNAL(scrollBy(int, int)), newDstList, SLOT(scrollBy(int, int)));
  connect(removeButton, SIGNAL(clicked()), SLOT(disconnectClicked()));
  connect(connectButton, SIGNAL(clicked()), SLOT(connectClicked()));
  connect(allMidiPortsButton, SIGNAL(clicked(bool)), SLOT(allMidiPortsClicked(bool)));
  connect(verticalLayoutButton, SIGNAL(clicked(bool)), SLOT(verticalLayoutClicked(bool)));
  connect(filterSrcButton, SIGNAL(clicked(bool)), SLOT(filterSrcClicked(bool)));
  connect(filterDstButton, SIGNAL(clicked(bool)), SLOT(filterDstClicked(bool)));
  connect(srcRoutesButton, SIGNAL(clicked(bool)), SLOT(filterSrcRoutesClicked(bool)));
  connect(dstRoutesButton, SIGNAL(clicked(bool)), SLOT(filterDstRoutesClicked(bool)));
  connect(routeAliasList, SIGNAL(activated(int)), SLOT(preferredRouteAliasChanged(int)));
  connect(MusEGlobal::song, SIGNAL(songChanged(MusECore::SongChangedFlags_t)), SLOT(songChanged(MusECore::SongChangedFlags_t)));
}

void RouteDialog::srcTreeScrollValueChanged(int value)
{
  // Prevent recursion
  srcTreeScrollBar->blockSignals(true);
  srcTreeScrollBar->setValue(value);
  srcTreeScrollBar->blockSignals(false);
}

void RouteDialog::dstTreeScrollValueChanged(int value)
{
  // Prevent recursion
  dstTreeScrollBar->blockSignals(true);
  dstTreeScrollBar->setValue(value);
  dstTreeScrollBar->blockSignals(false);
}

void RouteDialog::srcScrollBarValueChanged(int value)
{
  // Prevent recursion
  newSrcList->blockSignals(true);
  newSrcList->verticalScrollBar()->setValue(value);
  newSrcList->blockSignals(false);
}

void RouteDialog::dstScrollBarValueChanged(int value)
{
  // Prevent recursion
  newDstList->blockSignals(true);
  newDstList->verticalScrollBar()->setValue(value);
  newDstList->blockSignals(false);
}

// void RouteDialog::routeSplitterMoved(int pos, int index)
// {
//   DEBUG_PRST_ROUTES(stderr, "RouteDialog::routeSplitterMoved pos:%d index:%d\n", pos, index);  // REMOVE Tim.
//   
// }


void RouteDialog::preferredRouteAliasChanged(int /*idx*/)
{
  if(routeAliasList->currentData().canConvert<int>())
  {
    bool ok = false;
    const int n = routeAliasList->currentData().toInt(&ok);
    if(ok)
    {
      switch(n)
      {
        case MusEGlobal::RoutePreferCanonicalName:
        case MusEGlobal::RoutePreferFirstAlias:
        case MusEGlobal::RoutePreferSecondAlias:
          MusEGlobal::config.preferredRouteNameOrAlias = MusEGlobal::RouteNameAliasPreference(n);
          MusEGlobal::song->update(SC_PORT_ALIAS_PREFERENCE);
        break;
        default:
        break;
      }
    }
  }
}

void RouteDialog::verticalLayoutClicked(bool v)
{
  if(v)
  {
//     fprintf(stderr, "RouteDialog::verticalLayoutClicked v:%d calling src resizeColumnToContents\n", v); // REMOVE Tim.
//     newSrcList->resizeColumnToContents(ROUTE_NAME_COL);
//     fprintf(stderr, "RouteDialog::verticalLayoutClicked v:%d calling dst resizeColumnToContents\n", v); // REMOVE Tim.
//     newDstList->resizeColumnToContents(ROUTE_NAME_COL);
    MusEGlobal::config.routerExpandVertically = v;
    newSrcList->setWordWrap(v);
    newDstList->setWordWrap(v);
    newSrcList->setChannelWrap(v);
    newDstList->setChannelWrap(v);
    newSrcList->header()->setSectionResizeMode(QHeaderView::Stretch);
    newDstList->header()->setSectionResizeMode(QHeaderView::Stretch);
    newSrcList->setColumnWidth(ROUTE_NAME_COL, RouteChannelsList::minimumWidthHint());
    newDstList->setColumnWidth(ROUTE_NAME_COL, RouteChannelsList::minimumWidthHint());
  }
  else
  {
    MusEGlobal::config.routerExpandVertically = v;
    newSrcList->setWordWrap(v);
    newDstList->setWordWrap(v);
    newSrcList->setChannelWrap(true);
    newDstList->setChannelWrap(true);
    newSrcList->header()->setSectionResizeMode(QHeaderView::Interactive);
    newDstList->header()->setSectionResizeMode(QHeaderView::Interactive);
  }
  //fprintf(stderr, "RouteDialog::verticalLayoutClicked v:%d calling dst computeChannelYValues\n", v); // REMOVE Tim.
  newDstList->computeChannelYValues();
  newSrcList->computeChannelYValues();
  //fprintf(stderr, "RouteDialog::verticalLayoutClicked v:%d calling src computeChannelYValues\n", v); // REMOVE Tim.
  connectionsWidget->update();  // Redraw the connections.
}

void RouteDialog::allMidiPortsClicked(bool v)
{
  // TODO: This is a bit brutal and sweeping... Refine this down to needed parts only.
//   routingChanged();  
  
  
  // Refill the lists of available external ports.
//   tmpJackOutPorts = MusEGlobal::audioDevice->outputPorts();
//   tmpJackInPorts = MusEGlobal::audioDevice->inputPorts();
//   tmpJackMidiOutPorts = MusEGlobal::audioDevice->outputPorts(true);
//   tmpJackMidiInPorts = MusEGlobal::audioDevice->inputPorts(true);
  if(v)
    addItems();                   // Add any new items.
  else
    removeItems();                // Remove unused items.
    
//   newSrcList->resizeColumnToContents(ROUTE_NAME_COL);
//   newDstList->resizeColumnToContents(ROUTE_NAME_COL);
  routeList->resizeColumnToContents(ROUTE_SRC_COL);
  routeList->resizeColumnToContents(ROUTE_DST_COL);
  
  // Now that column resizing is done, update all channel y values in source and destination lists.
  // Must be done here because it relies on the column width.
  newDstList->computeChannelYValues();
  newSrcList->computeChannelYValues();
  
  routeSelectionChanged();      // Init remove button.
  srcSelectionChanged();        // Init select button.
  connectionsWidget->update();  // Redraw the connections.
}

void RouteDialog::filterSrcClicked(bool v)
{
//   if(v)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(newSrcList->currentItem());
//     filter(item, false);
//   }
//   else
//     filter(NULL, false);
  
  
  //if(v)
  //  _srcFilterItems = newSrcList->selectedItems();  
  //else
  //  _srcFilterItems.clear();
  
  if(dstRoutesButton->isChecked())
  {
    dstRoutesButton->blockSignals(true);
    dstRoutesButton->setChecked(false);
    dstRoutesButton->blockSignals(false);
  }
  filter(v ? newSrcList->selectedItems() : RouteTreeItemList(), RouteTreeItemList(), true, false);
//   if(v)
//   {
//     //if(dstRoutesButton->isEnabled())
//     //  dstRoutesButton->setEnabled(false);
//     filter(newSrcList->selectedItems(), RouteTreeItemList(), true, false);
//   }
//   else
//   {
//     //if(!dstRoutesButton->isEnabled())
//     //  dstRoutesButton->setEnabled(true);
//     filter(RouteTreeItemList(), RouteTreeItemList(), true, false);
//   }
}

void RouteDialog::filterDstClicked(bool v)
{
//   if(v)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(newDstList->currentItem());
//     filter(item, true);
//   }
//   else
//     filter(NULL, true);
  
//   if(v)
//     _dstFilterItems = newDstList->selectedItems();  
//   else
//     _dstFilterItems.clear();
//   
//   filter();

  if(srcRoutesButton->isChecked())
  {
    srcRoutesButton->blockSignals(true);
    srcRoutesButton->setChecked(false);
    srcRoutesButton->blockSignals(false);
  }
 filter(RouteTreeItemList(), v ? newDstList->selectedItems() : RouteTreeItemList(), false, true);
//   if(v)
//   {
//     //if(srcRoutesButton->isEnabled())
//     //  srcRoutesButton->setEnabled(false);
//     filter(RouteTreeItemList(), newDstList->selectedItems(), false, true);
//   }
//   else
//   {
//     //if(!srcRoutesButton->isEnabled())
//     //  srcRoutesButton->setEnabled(true);
//     filter(RouteTreeItemList(), RouteTreeItemList(), false, true);
//   }
}

void RouteDialog::filterSrcRoutesClicked(bool /*v*/)
{
//   if(v)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(newSrcList->currentItem());
//     filter(item, false);
//   }
//   else
//     filter(NULL, false);
  
  
//   if(v)
//     _srcFilterItems = newSrcList->selectedItems();  
//   else
//     _srcFilterItems.clear();
//   
//   filter();
  
  if(dstRoutesButton->isChecked())
  {
    dstRoutesButton->blockSignals(true);
    dstRoutesButton->setChecked(false);
    dstRoutesButton->blockSignals(false);
  }
  if(filterDstButton->isChecked())
  {
    filterDstButton->blockSignals(true);
    filterDstButton->setChecked(false);
    filterDstButton->blockSignals(false);
  }
  // Unfilter the entire destination list, while (un)filtering with the 'show only possible routes' part.
  filter(RouteTreeItemList(), RouteTreeItemList(), false, true);
//   if(v)
//   {
//     //if(filterSrcButton->isEnabled())
//     //  filterSrcButton->setEnabled(false);
//     filter(RouteTreeItemList(), newDstList->selectedItems(), false, true);
//   }
//   else
//   {
//     //if(!filterSrcButton->isEnabled())
//     //  filterSrcButton->setEnabled(true);
//     filter(RouteTreeItemList(), RouteTreeItemList(), false, true);
//   }
}

void RouteDialog::filterDstRoutesClicked(bool /*v*/)
{
//   if(v)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(newDstList->currentItem());
//     filter(item, true);
//   }
//   else
//     filter(NULL, true);
  
//   if(v)
//     _dstFilterItems = newDstList->selectedItems();  
//   else
//     _dstFilterItems.clear();
//   
//   filter();
//   filter(RouteTreeItemList(), 
//          filterDstButton->isChecked() ? newDstList->selectedItems() : RouteTreeItemList(),
//          false, true);
  
  if(srcRoutesButton->isChecked())
  {
    srcRoutesButton->blockSignals(true);
    srcRoutesButton->setChecked(false);
    srcRoutesButton->blockSignals(false);
  }
  if(filterSrcButton->isChecked())
  {
    filterSrcButton->blockSignals(true);
    filterSrcButton->setChecked(false);
    filterSrcButton->blockSignals(false);
  }
  // Unfilter the entire source list, while (un)filtering with the 'show only possible routes' part.
  filter(RouteTreeItemList(), RouteTreeItemList(), true, false);
}

void RouteDialog::filter(const RouteTreeItemList& srcFilterItems, 
                         const RouteTreeItemList& dstFilterItems,
                         bool filterSrc, 
                         bool filterDst)
{
  bool src_changed = false;
  bool dst_changed = false;
  const RouteTreeItemList src_sel_items = newSrcList->selectedItems();
  const RouteTreeItemList dst_sel_items = newDstList->selectedItems();
  bool hide;

  QTreeWidgetItemIterator iSrcTree(newSrcList);
  while(*iSrcTree)
  {
    QTreeWidgetItem* item = *iSrcTree;
    hide = item->isHidden();

    if(filterSrc)
    {
      hide = false;
      if(!srcFilterItems.isEmpty())
      {
        RouteTreeItemList::const_iterator ciFilterItems = srcFilterItems.cbegin();
        for( ; ciFilterItems != srcFilterItems.cend(); ++ciFilterItems)
        {
          QTreeWidgetItem* flt_item = *ciFilterItems;
          QTreeWidgetItem* twi = flt_item;
          while(twi != item && (twi = twi->parent()))  ;
          
          if(twi == item)
            break;
          QTreeWidgetItem* twi_p = item;
          while(twi_p != flt_item && (twi_p = twi_p->parent()))  ;

          if(twi_p == flt_item)
            break;
        }
        hide = ciFilterItems == srcFilterItems.cend();
      }
    }
    //else
    
    if(!filterSrc || srcFilterItems.isEmpty())
    {
      hide = false;
      //// Is the item slated to hide? Check finally the 'show only possible routes' settings...
      //if(hide && dstRoutesButton->isChecked())
      // Check finally the 'show only possible routes' settings...
      if(dstRoutesButton->isChecked())
      {
        switch(item->type())
        {
          case RouteTreeWidgetItem::NormalItem:
          case RouteTreeWidgetItem::CategoryItem:
            hide = true;
          break;
          
          case RouteTreeWidgetItem::RouteItem:
          case RouteTreeWidgetItem::ChannelsItem:
          {
            RouteTreeWidgetItem* rtwi = static_cast<RouteTreeWidgetItem*>(item);
            RouteTreeItemList::const_iterator iSelItems = dst_sel_items.cbegin();
            for( ; iSelItems != dst_sel_items.cend(); ++iSelItems)
            {
              RouteTreeWidgetItem* sel_dst_item = static_cast<RouteTreeWidgetItem*>(*iSelItems);
              //if(sel_dst_item->type() == item->type() && MusECore::routesCompatible(rtwi->route(), sel_dst_item->route(), false))
              if(MusECore::routesCompatible(rtwi->route(), sel_dst_item->route(), true))
              {
                //hide = false;
                // Hm, Qt doesn't seem to do this for us:
                QTreeWidgetItem* twi = item;
                while((twi = twi->parent()))
                {
                  //if(twi->isHidden() != hide)
                  if(twi->isHidden())
                  {
                    //twi->setHidden(hide);
                    twi->setHidden(false);
                    src_changed = true;
                  }
                }
                break;
              }
            }
            hide = iSelItems == dst_sel_items.cend();
          }
          break;
        }
      }
    }
    
    if(item->isHidden() != hide)
    {
      item->setHidden(hide);
      src_changed = true;
    }
    
    ++iSrcTree;
  }

  
  QTreeWidgetItemIterator iDstTree(newDstList);
  while(*iDstTree)
  {
    QTreeWidgetItem* item = *iDstTree;
    hide = item->isHidden();
    
    if(filterDst)
    {
      hide = false;
      if(!dstFilterItems.isEmpty())
      {
        RouteTreeItemList::const_iterator ciFilterItems = dstFilterItems.cbegin();
        for( ; ciFilterItems != dstFilterItems.cend(); ++ciFilterItems)
        {
          QTreeWidgetItem* flt_item = *ciFilterItems;
          QTreeWidgetItem* twi = flt_item;
          while(twi != item && (twi = twi->parent()))  ;
          
          if(twi == item)
            break;
          QTreeWidgetItem* twi_p = item;
          while(twi_p != flt_item && (twi_p = twi_p->parent()))  ;
          
          if(twi_p == flt_item)
            break;
        }
        hide = ciFilterItems == dstFilterItems.cend();
      }
    }
    //else
    
    if(!filterDst || dstFilterItems.isEmpty())
    {
      hide = false;
      //// Is the item slated to hide? Check finally the 'show only possible routes' settings...
      //if(hide && srcRoutesButton->isChecked())
      // Check finally the 'show only possible routes' settings...
      if(srcRoutesButton->isChecked())
      {
        switch(item->type())
        {
          case RouteTreeWidgetItem::NormalItem:
          case RouteTreeWidgetItem::CategoryItem:
            hide = true;
          break;
          
          case RouteTreeWidgetItem::RouteItem:
          case RouteTreeWidgetItem::ChannelsItem:
          {
            RouteTreeWidgetItem* rtwi = static_cast<RouteTreeWidgetItem*>(item);
            RouteTreeItemList::const_iterator iSelItems = src_sel_items.cbegin();
            for( ; iSelItems != src_sel_items.cend(); ++iSelItems)
            {
              RouteTreeWidgetItem* sel_src_item = static_cast<RouteTreeWidgetItem*>(*iSelItems);

              MusECore::Route& src = sel_src_item->route();
              MusECore::Route& dst = rtwi->route();
              
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
// Special: Allow simulated midi track to midi port route (a route found in our 'local' routelist
//           but not in any track or port routelist) until multiple output routes are allowed
//           instead of just single port and channel properties. The route is exclusive.
              bool is_compatible = false;
              switch(src.type)
              {
                case MusECore::Route::TRACK_ROUTE:
                  switch(dst.type)
                  {
                    case MusECore::Route::MIDI_PORT_ROUTE:
                      if(src.track->isMidiTrack())
                        is_compatible = true;
                    break;
                    
                    case MusECore::Route::TRACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
                      break;
                  }
                break;
                
                case MusECore::Route::MIDI_PORT_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
                break;
              }
              
              if(is_compatible ||
#else
              if(
#endif
                 MusECore::routesCompatible(sel_src_item->route(), rtwi->route(), true))
              {
                //hide = false;
                // Hm, Qt doesn't seem to do this for us:
                QTreeWidgetItem* twi = item;
                while((twi = twi->parent()))
                {
                  //if(twi->isHidden() != hide)
                  if(twi->isHidden())
                  {
                    //twi->setHidden(hide);
                    twi->setHidden(false);
                    dst_changed = true;
                  }
                }
                break;
              }
            }
            hide = iSelItems == src_sel_items.cend();
          }
          break;
        }
      }
    }
    
    if(item->isHidden() != hide)
    {
      item->setHidden(hide);
      dst_changed = true;
    }
    
    ++iDstTree;
  }
  
  
  
  // Update the connecting lines.
  if(src_changed)
  {
//     QTreeWidgetItemIterator iSrcList(newSrcList);
//     while(*iSrcList)
//     {
//       RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(*iSrcList);
//       item->computeChannelYValues();
//       ++iSrcList;
//     }
    newSrcList->computeChannelYValues();
  }
  if(dst_changed)
  {
//     QTreeWidgetItemIterator iDstList(newDstList);
//     while(*iDstList)
//     {
//       RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(*iDstList);
//       item->computeChannelYValues();
//       ++iDstList;
//     }
    newDstList->computeChannelYValues();
  }
  
  if(src_changed || dst_changed)  
    connectionsWidget->update();  // Redraw the connections.
  
  QTreeWidgetItemIterator iRouteTree(routeList);
//   //RouteTreeItemList::iterator iFilterItems;
//   //filterItems::iterator iFilterItems;
//   
  while(*iRouteTree)
  {
    QTreeWidgetItem* item = *iRouteTree;
    //if(item->data(isDestination ? RouteDialog::ROUTE_DST_COL : RouteDialog::ROUTE_SRC_COL, 
    //              RouteDialog::RouteRole).canConvert<MusECore::Route>())
    if(item && item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && item->data(ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
    {        
      //const MusECore::Route r = item->data(isDestination ? RouteDialog::ROUTE_DST_COL : RouteDialog::ROUTE_SRC_COL, 
      //                                     RouteDialog::RouteRole).value<MusECore::Route>();
      const MusECore::Route src = item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>();
      const MusECore::Route dst = item->data(ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
      
//       bool hide = false;
//       
//       //if(_srcFilterItems.isEmpty())
//       //  item->setHidden(false);
//       //else
//       //{
//         RouteTreeItemList::const_iterator ciFilterItems = _srcFilterItems.begin();
//         for( ; ciFilterItems != _srcFilterItems.end(); ++ciFilterItems)
//           if(src.compare(static_cast<RouteTreeWidgetItem*>(*ciFilterItems)->route()))  // FIXME Ugly
//             break;
//         if(ciFilterItems == _srcFilterItems.end())
//           hide = true;
//         item->setHidden(ciFilterItems == filterItems.end());
//       //}
        
      RouteTreeWidgetItem* src_item = newSrcList->findItem(src);
      RouteTreeWidgetItem* dst_item = newDstList->findItem(dst);
      item->setHidden((src_item && src_item->isHidden()) || (dst_item && dst_item->isHidden()));
    }
    ++iRouteTree;
  }
  
  //routingChanged();
}

//---------------------------------------------------------
//   songChanged
//---------------------------------------------------------

void RouteDialog::songChanged(MusECore::SongChangedFlags_t v)
{
  if(v & SC_PORT_ALIAS_PREFERENCE)
  {
    const int idx = routeAliasList->findData(QVariant::fromValue<int>(MusEGlobal::config.preferredRouteNameOrAlias));
    if(idx != -1 && idx != routeAliasList->currentIndex())
    {
      routeAliasList->blockSignals(true);
      routeAliasList->setCurrentIndex(idx);
      routeAliasList->blockSignals(false);
    }
  }
  
  if(v & (SC_ROUTE | SC_CONFIG))
  {
    // Refill the lists of available external ports.
    tmpJackOutPorts = MusEGlobal::audioDevice->outputPorts();
    tmpJackInPorts = MusEGlobal::audioDevice->inputPorts();
    tmpJackMidiOutPorts = MusEGlobal::audioDevice->outputPorts(true);
    tmpJackMidiInPorts = MusEGlobal::audioDevice->inputPorts(true);
  }
  
  if(v & (SC_TRACK_INSERTED | SC_TRACK_REMOVED | SC_TRACK_MODIFIED | SC_MIDI_TRACK_PROP | 
          SC_ROUTE | SC_CONFIG | SC_CHANNELS | SC_PORT_ALIAS_PREFERENCE)) 
  {
    removeItems();                // Remove unused items.
    addItems();                   // Add any new items.
    routeList->resizeColumnToContents(ROUTE_SRC_COL);
    routeList->resizeColumnToContents(ROUTE_DST_COL);
    
    // Now that column resizing is done, update all channel y values in source and destination lists.
    newDstList->computeChannelYValues();
    newSrcList->computeChannelYValues();
//     newDstList->scheduleDelayedLayout();
//     newSrcList->scheduleDelayedLayout();
    
    routeSelectionChanged();      // Init remove button.
    srcSelectionChanged();        // Init select button.
    connectionsWidget->update();  // Redraw the connections.
  }
}

//---------------------------------------------------------
//   routeSelectionChanged
//---------------------------------------------------------

void RouteDialog::routeSelectionChanged()
{
  QTreeWidgetItem* item = routeList->currentItem();
  if(item == 0)
  {
    connectButton->setEnabled(false);
    removeButton->setEnabled(false);
    return;
  }
  if(!item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() || !item->data(ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
  {
    connectButton->setEnabled(false);
    removeButton->setEnabled(false);
    return;
  }
  const MusECore::Route src = item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>();
  const MusECore::Route dst = item->data(ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
  RouteTreeWidgetItem* srcItem = newSrcList->findItem(src);
  RouteTreeWidgetItem* dstItem = newDstList->findItem(dst);
  newSrcList->blockSignals(true);
  newSrcList->setCurrentItem(srcItem);
  newSrcList->blockSignals(false);
  newDstList->blockSignals(true);
  newDstList->setCurrentItem(dstItem);
  newDstList->blockSignals(false);
  selectRoutes(true);
  if(srcItem)
    newSrcList->scrollToItem(srcItem, QAbstractItemView::PositionAtCenter);
    //newSrcList->scrollToItem(srcItem, QAbstractItemView::EnsureVisible);
  if(dstItem)
    newDstList->scrollToItem(dstItem, QAbstractItemView::PositionAtCenter);
    //newDstList->scrollToItem(dstItem, QAbstractItemView::EnsureVisible);
  connectionsWidget->update();
//   connectButton->setEnabled(MusECore::routeCanConnect(src, dst));
  connectButton->setEnabled(false);
//   removeButton->setEnabled(MusECore::routeCanDisconnect(src, dst));
//   removeButton->setEnabled(true);
  
  
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
// Special: Allow simulated midi track to midi port route (a route found in our 'local' routelist
//           but not in any track or port routelist) until multiple output routes are allowed
//           instead of just single port and channel properties. The route is exclusive.
      switch(src.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          switch(dst.type)
          {
            case MusECore::Route::MIDI_PORT_ROUTE:
              if(src.track->isMidiTrack())
              {
                MusECore::MidiTrack* mt = static_cast<MusECore::MidiTrack*>(src.track);
                // We cannot 'remove' a simulated midi track output port and channel route.
                // (Midi port cannot be -1 meaning 'no port'.)
                // Only remove it if it's a different port or channel. 
                removeButton->setEnabled(mt->outPort() != dst.midiPort || mt->outChannel() != src.channel);
                return;
              }
            break;
            
            case MusECore::Route::TRACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
              break;
          }
        break;
        
        case MusECore::Route::MIDI_PORT_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
        break;
      }
#endif
  
  removeButton->setEnabled(true);
  
}

//---------------------------------------------------------
//   disconnectClicked
//---------------------------------------------------------

void RouteDialog::disconnectClicked()
{
//   QTreeWidgetItem* item = routeList->currentItem();
//   if(item && item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && item->data(ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//   {
//     const MusECore::Route src = item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     const MusECore::Route dst = item->data(ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     MusEGlobal::audio->msgRemoveRoute(src, dst);
//     MusEGlobal::audio->msgUpdateSoloStates();
//     MusEGlobal::song->update(SC_SOLO);
//   }
//   routingChanged();
// 
//   
//   const int cnt = routeList->topLevelItemCount(); 
//   for(int i = 0; i < cnt; ++i)
//   {
//     QTreeWidgetItem* item = routeList->topLevelItem(i);
//     if(!item || !item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() || !item->data(ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//       continue;
//     if(item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>() == src && item->data(ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>() == dst)
//       return item;
//   }
  
  
  MusECore::PendingOperationList operations;
  QTreeWidgetItemIterator ii(routeList);
  while(*ii)
  {
    QTreeWidgetItem* item = *ii;
    if(item && item->isSelected() &&
       item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && 
       item->data(ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
    {
      const MusECore::Route src = item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>();
      const MusECore::Route dst = item->data(ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
      //if(MusECore::routeCanDisconnect(src, dst))
//         operations.add(MusECore::PendingOperationItem(src, dst, MusECore::PendingOperationItem::DeleteRoute));
        
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
// Special: Allow simulated midi track to midi port route (a route found in our 'local' routelist
//           but not in any track or port routelist) until multiple output routes are allowed
//           instead of just single port and channel properties. The route is exclusive.
      switch(src.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          switch(dst.type)
          {
            case MusECore::Route::MIDI_PORT_ROUTE:
              if(src.track->isMidiTrack())
              {
//                 MusECore::MidiTrack* mt = static_cast<MusECore::MidiTrack*>(src.track);
                // We cannot 'remove' a simulated midi track output port and channel route.
                // (Midi port cannot be -1 meaning 'no port'.)
                // Only remove it if it's a different port or channel. 
//                 if(mt->outPort() != dst.midiPort || mt->outChannel() != src.channel)
//                   operations.add(MusECore::PendingOperationItem(src, dst, MusECore::PendingOperationItem::DeleteRoute));
                ++ii;
                continue;
              }
            break;
            
            case MusECore::Route::TRACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
              break;
          }
        break;
        
        case MusECore::Route::MIDI_PORT_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
        break;
      }
#endif
        
      operations.add(MusECore::PendingOperationItem(src, dst, MusECore::PendingOperationItem::DeleteRoute));
        
    }      
    ++ii;
  }
  
  if(!operations.empty())
  {
    operations.add(MusECore::PendingOperationItem((MusECore::TrackList*)NULL, MusECore::PendingOperationItem::UpdateSoloStates));
    MusEGlobal::audio->msgExecutePendingOperations(operations, true);
//     MusEGlobal::song->update(SC_ROUTE);
    //MusEGlobal::song->update(SC_SOLO);
    //routingChanged();
  }
  
  
//   QTreeWidgetItem* srcItem = newSrcList->currentItem();
//   QTreeWidgetItem* dstItem = newDstList->currentItem();
//   if(srcItem == 0 || dstItem == 0)
//     return;
//   if(srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//   {        
//     MusECore::Route src = srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     MusECore::Route dst = dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     MusEGlobal::audio->msgRemoveRoute(src, dst);
//     MusEGlobal::audio->msgUpdateSoloStates();
//     MusEGlobal::song->update(SC_SOLO);
//   }
//   routingChanged();
}

//---------------------------------------------------------
//   connectClicked
//---------------------------------------------------------

void RouteDialog::connectClicked()
{
//   RouteTreeWidgetItem* srcItem = static_cast<RouteTreeWidgetItem*>(newSrcList->currentItem());
//   RouteTreeWidgetItem* dstItem = static_cast<RouteTreeWidgetItem*>(newDstList->currentItem());
//   if(srcItem == 0 || dstItem == 0)
//     return;
//   
//   const MusECore::Route src = srcItem->route();
//   const MusECore::Route dst = dstItem->route();
//   MusEGlobal::audio->msgAddRoute(src, dst);
//   MusEGlobal::audio->msgUpdateSoloStates();
//   MusEGlobal::song->update(SC_SOLO);
//   routingChanged();
  
  MusECore::PendingOperationList operations;
  MusECore::RouteList srcList;
  MusECore::RouteList dstList;
  newSrcList->getSelectedRoutes(srcList);
  newDstList->getSelectedRoutes(dstList);
  const int srcSelSz = srcList.size();
  const int dstSelSz = dstList.size();
  bool upd_trk_props = false;

  for(int srcIdx = 0; srcIdx < srcSelSz; ++srcIdx)
  {
    const MusECore::Route& src = srcList.at(srcIdx);
    for(int dstIdx = 0; dstIdx < dstSelSz; ++dstIdx)
    {
      const MusECore::Route& dst = dstList.at(dstIdx);
      
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
// Special: Allow simulated midi track to midi port route (a route found in our 'local' routelist
//           but not in any track or port routelist) until multiple output routes are allowed
//           instead of just single port and channel properties. The route is exclusive.
      switch(src.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          switch(dst.type)
          {
            case MusECore::Route::MIDI_PORT_ROUTE:
              if(src.track->isMidiTrack())
              {
                MusECore::MidiTrack* mt = static_cast<MusECore::MidiTrack*>(src.track);
                // We cannot 'remove' a simulated midi track output port and channel route.
                // (Midi port cannot be -1 meaning 'no port'.)
                // Only remove it if it's a different port or channel. 
                if(src.channel >= 0 && src.channel < MIDI_CHANNELS && (mt->outPort() != dst.midiPort || mt->outChannel() != src.channel))
                {
                  MusEGlobal::audio->msgIdle(true);
                  mt->setOutPortAndChannelAndUpdate(dst.midiPort, src.channel);
                  MusEGlobal::audio->msgIdle(false);
                  //MusEGlobal::audio->msgUpdateSoloStates();
                  //MusEGlobal::song->update(SC_MIDI_TRACK_PROP);
                  upd_trk_props = true;
                }
                continue;
              }
            break;
            
            case MusECore::Route::TRACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
              break;
          }
        break;
        
        case MusECore::Route::MIDI_PORT_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
        break;
      }
#endif
      
      if(MusECore::routeCanConnect(src, dst))
        operations.add(MusECore::PendingOperationItem(src, dst, MusECore::PendingOperationItem::AddRoute));
    }
  }

  if(!operations.empty())
  {
    operations.add(MusECore::PendingOperationItem((MusECore::TrackList*)NULL, MusECore::PendingOperationItem::UpdateSoloStates));
    MusEGlobal::audio->msgExecutePendingOperations(operations, true, upd_trk_props ? SC_MIDI_TRACK_PROP : 0);
//     MusEGlobal::song->update(SC_ROUTE | (upd_trk_props ? SC_MIDI_TRACK_PROP : 0));
    //MusEGlobal::song->update(SC_SOLO);
    //routingChanged();
  }
  else if(upd_trk_props)
    MusEGlobal::song->update(SC_MIDI_TRACK_PROP);
    
}  
//   if(srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//   {    
//     const MusECore::Route src = srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     const MusECore::Route dst = dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     MusEGlobal::audio->msgAddRoute(src, dst);
//     MusEGlobal::audio->msgUpdateSoloStates();
//     MusEGlobal::song->update(SC_SOLO);
//   }
//   routingChanged();
// }

//---------------------------------------------------------
//   srcSelectionChanged
//---------------------------------------------------------

void RouteDialog::srcSelectionChanged()
{
  DEBUG_PRST_ROUTES(stderr, "RouteDialog::srcSelectionChanged\n");  // REMOVE Tim.

  MusECore::RouteList srcList;
  MusECore::RouteList dstList;
  newSrcList->getSelectedRoutes(srcList);
  newDstList->getSelectedRoutes(dstList);
  const int srcSelSz = srcList.size();
  const int dstSelSz = dstList.size();
  //if(srcSelSz == 0 || dstSelSz == 0 || (srcSelSz > 1 && dstSelSz > 1))
  //{
  //  connectButton->setEnabled(false);
  //  removeButton->setEnabled(false);
  //  return;
  //}

  routeList->blockSignals(true);
  routeList->clearSelection();
  bool canConnect = false;
  QTreeWidgetItem* routesItem = 0;
  int routesSelCnt = 0;
  int routesRemoveCnt = 0;
  for(int srcIdx = 0; srcIdx < srcSelSz; ++srcIdx)
  {
    MusECore::Route& src = srcList.at(srcIdx);
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
    int mt_to_mp_cnt = 0;
#endif
    for(int dstIdx = 0; dstIdx < dstSelSz; ++dstIdx)
    {
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
      bool useMTOutProps = false;
#endif
      MusECore::Route& dst = dstList.at(dstIdx);
      // Special for some item type combos: Before searching, cross-update the routes if necessary.
      // To minimize route copying, here on each iteration we alter each list route, but should be OK.
      switch(src.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          switch(dst.type)
          {
            case MusECore::Route::MIDI_PORT_ROUTE:
              if(src.track->isMidiTrack())
              {
                dst.channel = src.channel;
                
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
// Special: Allow simulated midi track to midi port route (a route found in our 'local' routelist
//           but not in any track or port routelist) until multiple output routes are allowed
//           instead of just single port and channel properties. The route is exclusive.
                useMTOutProps = true;
                MusECore::MidiTrack* mt = static_cast<MusECore::MidiTrack*>(src.track);
                if(src.channel >= 0 && src.channel < MIDI_CHANNELS && (mt->outPort() != dst.midiPort || mt->outChannel() != src.channel))
                  ++mt_to_mp_cnt;
#endif
                
              }  
            break;
            
            case MusECore::Route::TRACK_ROUTE: case MusECore::Route::JACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: break;
          }
        break;

        case MusECore::Route::MIDI_PORT_ROUTE:
          switch(dst.type)
          {
            case MusECore::Route::TRACK_ROUTE: src.channel = dst.channel; break;
            case MusECore::Route::MIDI_PORT_ROUTE: case MusECore::Route::JACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: break;
          }
        break;
        
        case MusECore::Route::JACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: break;
      }
      
      QTreeWidgetItem* ri = findRoutesItem(src, dst);
      if(ri)
      {
        ri->setSelected(true);
        routesItem = ri;
        ++routesSelCnt;
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
        if(!useMTOutProps)
#endif
          ++routesRemoveCnt;
      }
      
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
      if(useMTOutProps)
        continue;
#endif
      
      if(MusECore::routeCanConnect(src, dst))
        canConnect = true;
//       if(MusECore::routeCanDisconnect(src, dst))
//         canDisconnect = true;
    }
    
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
    // If there was one and only one selected midi port for a midi track, allow (simulated) connection.
    if(mt_to_mp_cnt == 1)
      canConnect = true;
#endif
    
  }
  
  if(routesSelCnt == 0)
    routeList->setCurrentItem(0);
  //routeList->setCurrentItem(routesItem);
  routeList->blockSignals(false);
  if(routesSelCnt == 1)
    routeList->scrollToItem(routesItem, QAbstractItemView::PositionAtCenter);

  selectRoutes(false);
  
  connectionsWidget->update();
  //connectButton->setEnabled(can_connect && (srcSelSz == 1 || dstSelSz == 1));
  connectButton->setEnabled(canConnect);
  //removeButton->setEnabled(can_disconnect);
//   removeButton->setEnabled(routesSelCnt > 0);
  removeButton->setEnabled(routesRemoveCnt != 0);

/*  
  RouteTreeItemList srcSel = newSrcList->selectedItems();
  RouteTreeItemList dstSel = newDstList->selectedItems();
  const int srcSelSz = srcSel.size();
  const int dstSelSz = dstSel.size();
  if(srcSelSz == 0 || dstSelSz == 0)
  {
    connectButton->setEnabled(false);
    removeButton->setEnabled(false);
    return;
  }

  bool can_connect = false;
  bool can_disconnect = false;
  for(int srcIdx = 0; srcIdx < srcSelSz; ++srcIdx)
  {
    QTreeWidgetItem* srcItem = srcSel.at(srcIdx);
    if(!srcItem)
      continue;
    if(!srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
      continue;
    MusECore::Route src = srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
    if(srcItem->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
    {
      QBitArray ba = srcItem->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
      switch(src.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          if(src.track && src.track->isMidiTrack())
          {
            int chans = 0;
            const int sz = ba.size();
            for(int i = 0; i < sz; ++i)
            {
              if(i >= MIDI_CHANNELS)
                break;
              if(ba.testBit(i))
                src.channel |= (1 << i);
            }
          }
        break;
        case MusECore::Route::JACK_ROUTE:
        case MusECore::Route::MIDI_DEVICE_ROUTE:
        case MusECore::Route::MIDI_PORT_ROUTE:
        break;
      }
    }
    
    for(int dstIdx = 0; dstIdx < dstSelSz; ++dstIdx)
    {
      QTreeWidgetItem* dstItem = dstSel.at(dstIdx);
      if(!dstItem)
        continue;
      if(!dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
        continue;
      MusECore::Route dst = dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
      if(dstItem->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
      {
        QBitArray ba = dstItem->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
      }
      
      
      
    }
    
    
    
  }
  
  
  QTreeWidgetItem* srcItem = newSrcList->currentItem();
  QTreeWidgetItem* dstItem = newDstList->currentItem();
  if(srcItem == 0 || dstItem == 0)
  {
    connectButton->setEnabled(false);
    removeButton->setEnabled(false);
    return;
  }
  if(!srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() || !dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
  {
    connectButton->setEnabled(false);
    removeButton->setEnabled(false);
    return;
  }
  //const MusECore::Route src = srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
  //const MusECore::Route dst = dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
  MusECore::Route src = srcItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
  MusECore::Route dst = dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
  if(srcItem->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
  {
    QBitArray ba = srcItem->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
  }
    
//     || !dstItem->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//   {
//     connectButton->setEnabled(false);
//     removeButton->setEnabled(false);
//     return;
//   }
  QTreeWidgetItem* routesItem = findRoutesItem(src, dst);
  routeList->blockSignals(true);
  routeList->setCurrentItem(routesItem);
  routeList->blockSignals(false);
  if(routesItem)
    routeList->scrollToItem(routesItem, QAbstractItemView::PositionAtCenter);
  connectionsWidget->update();
  connectButton->setEnabled(MusECore::routeCanConnect(src, dst));
  removeButton->setEnabled(MusECore::routeCanDisconnect(src, dst));*/
}

//---------------------------------------------------------
//   dstSelectionChanged
//---------------------------------------------------------

void RouteDialog::dstSelectionChanged()
{
  srcSelectionChanged();  
}

//---------------------------------------------------------
//   closeEvent
//---------------------------------------------------------

void RouteDialog::closeEvent(QCloseEvent* e)
{
  emit closed();
  e->accept();
}

// RouteTreeWidgetItem* RouteDialog::findSrcItem(const MusECore::Route& src)
// {
//   int cnt = newSrcList->topLevelItemCount(); 
//   for(int i = 0; i < cnt; ++i)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(newSrcList->topLevelItem(i));
//     if(item)
//     {
//       if(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//       {
//         //if(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>() == src)
//         if(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>().compare(src))
//           return item;
//       }
// 
//       int c_cnt = item->childCount();
//       for(int j = 0; j < c_cnt; ++j)
//       {
//         RouteTreeWidgetItem* c_item = static_cast<RouteTreeWidgetItem*>(item->child(j));
//         if(c_item)
//         {
//           if(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//           {
//             //if(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>() == src)
//             if(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>().compare(src))
//               return c_item;
//           }
// 
//           int cc_cnt = c_item->childCount();
//           for(int k = 0; k < cc_cnt; ++k)
//           {
//             RouteTreeWidgetItem* cc_item = static_cast<RouteTreeWidgetItem*>(c_item->child(k));
//             if(cc_item)
//             {
//               if(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//               {
//                 //if(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>() == src)
//                 if(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>().compare(src))
//                   return cc_item;
//               }
//             }
//           }
//         }
//       }
//     }
//   }
//   return 0;
// }

// RouteTreeWidgetItem* RouteDialog::findDstItem(const MusECore::Route& dst)
// {
//   int cnt = newDstList->topLevelItemCount(); 
//   for(int i = 0; i < cnt; ++i)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(newDstList->topLevelItem(i));
//     if(item)
//     {
//       if(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//       {
//         //if(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>() == dst)
//         if(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>().compare(dst))
//           return item;
//       }
// 
//       int c_cnt = item->childCount();
//       for(int j = 0; j < c_cnt; ++j)
//       {
//         RouteTreeWidgetItem* c_item = static_cast<RouteTreeWidgetItem*>(item->child(j));
//         if(c_item)
//         {
//           if(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//           {
//             //if(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>() == dst)
//             if(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>().compare(dst))
//               return c_item;
//           }
// 
//           int cc_cnt = c_item->childCount();
//           for(int k = 0; k < cc_cnt; ++k)
//           {
//             RouteTreeWidgetItem* cc_item = static_cast<RouteTreeWidgetItem*>(c_item->child(k));
//             if(cc_item)
//             {
//               if(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//               {
//                 //if(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>() == dst)
//                 if(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>().compare(dst))
//                   return cc_item;
//               }
//             }
//           }
//         }
//       }
//     }
//   }
//   return 0;
// }

QTreeWidgetItem* RouteDialog::findRoutesItem(const MusECore::Route& src, const MusECore::Route& dst)
{
  int cnt = routeList->topLevelItemCount(); 
  for(int i = 0; i < cnt; ++i)
  {
    QTreeWidgetItem* item = routeList->topLevelItem(i);
    if(!item || !item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() || !item->data(ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
      continue;
    if(item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>() == src && item->data(ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>() == dst)
      return item;
  }
  return 0;
}
      
// RouteTreeWidgetItem* RouteDialog::findCategoryItem(QTreeWidget* tree, const QString& name)
// {
//   int cnt = tree->topLevelItemCount(); 
//   for(int i = 0; i < cnt; ++i)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(tree->topLevelItem(i));
//     if(item && item->text(ROUTE_NAME_COL) == name)
//       return item;
//   }
//   return 0;
// }
      
// void RouteDialog::getSelectedRoutes(QTreeWidget* tree, MusECore::RouteList& routes)
// {
//   //DEBUG_PRST_ROUTES(stderr, "RouteDialog::getSelectedRoutes\n");  // REMOVE Tim.
//   
//   RouteTreeItemList sel = tree->selectedItems();
//   const int selSz = sel.size();
//   if(selSz == 0)
//     return;
// 
//   for(int idx = 0; idx < selSz; ++idx)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(sel.at(idx));
//     if(!item)
//       continue;
//     if(!item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//       continue;
//     MusECore::Route r = item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>();
//     if(item->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).canConvert<QBitArray>())
//     {
//       QBitArray ba = item->data(ROUTE_NAME_COL, RouteDialog::ChannelsRole).value<QBitArray>();
//       switch(r.type)
//       {
//         case MusECore::Route::TRACK_ROUTE:
//           if(r.track)
//           {
//             const int sz = ba.size();
//             if(r.track->isMidiTrack())
//             {  
//               for(int i = 0; i < sz; ++i)
//               {
//                 if(i >= MIDI_CHANNELS)
//                   break;
//                 if(ba.testBit(i))
//                 {
//                   r.channel = (1 << i);
//                   routes.push_back(r);
//                 }
//               }
//             }
//             else
//             {
//               for(int i = 0; i < sz; ++i)
//               {
//                 if(ba.testBit(i))
//                 {
//                   r.channel = i;
//                   routes.push_back(r);
//                 }
//               }
//             }
//           }
//         break;
//         case MusECore::Route::JACK_ROUTE:
//         case MusECore::Route::MIDI_DEVICE_ROUTE:
//         case MusECore::Route::MIDI_PORT_ROUTE:
//         break;
//       }
//     }
//     else
//       routes.push_back(r);
//   }
// }
      
      
      
      
void RouteDialog::removeItems()
{
  QVector<QTreeWidgetItem*> itemsToDelete;
  
  newSrcList->getItemsToDelete(itemsToDelete);
  newDstList->getItemsToDelete(itemsToDelete);
  getRoutesToDelete(routeList, itemsToDelete);


  newSrcList->blockSignals(true);
  newDstList->blockSignals(true);
  routeList->blockSignals(true);
  
  if(!itemsToDelete.empty())
  {
    int cnt = itemsToDelete.size();
    for(int i = 0; i < cnt; ++i)
      delete itemsToDelete.at(i);
  }

  selectRoutes(false);
  
  routeList->blockSignals(false);
  newDstList->blockSignals(false);
  newSrcList->blockSignals(false);
  
  //connectionsWidget->update();
}

// void RouteDialog::getItemsToDelete(QTreeWidget* tree, QVector<QTreeWidgetItem*>& items_to_remove)
// {
//   int cnt = tree->topLevelItemCount(); 
//   for(int i = 0; i < cnt; ++i)
//   {
//     RouteTreeWidgetItem* item = static_cast<RouteTreeWidgetItem*>(tree->topLevelItem(i));
//     if(item)
//     {
//       int c_cnt = item->childCount();
//       for(int j = 0; j < c_cnt; ++j)
//       {
//         RouteTreeWidgetItem* c_item = static_cast<RouteTreeWidgetItem*>(item->child(j));
//         if(c_item)
//         {
//           int cc_cnt = c_item->childCount();
//           for(int k = 0; k < cc_cnt; ++k)
//           {
//             RouteTreeWidgetItem* cc_item = static_cast<RouteTreeWidgetItem*>(c_item->child(k));
//             if(cc_item)
//             {
//               if(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//               {
//                 if(!routeNodeExists(cc_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>()))
//                   items_to_remove.append(cc_item);
//               }
//             }
//           }
//           if(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//           {
//             if(!routeNodeExists(c_item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>()))
//               items_to_remove.append(c_item);
//           }
//         }
//       }
//       if(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
//       {
//         if(!routeNodeExists(item->data(ROUTE_NAME_COL, RouteDialog::RouteRole).value<MusECore::Route>()))
//           items_to_remove.append(item);
//       }
//     }
//   }
// }

void RouteDialog::getRoutesToDelete(QTreeWidget* tree, QVector<QTreeWidgetItem*>& items_to_remove)
{
  const int iItemCount = tree->topLevelItemCount();
  for (int iItem = 0; iItem < iItemCount; ++iItem) 
  {
    QTreeWidgetItem *item = tree->topLevelItem(iItem);
    if(item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>() && item->data(ROUTE_DST_COL, RouteDialog::RouteRole).canConvert<MusECore::Route>())
    {        
      const MusECore::Route src = item->data(ROUTE_SRC_COL, RouteDialog::RouteRole).value<MusECore::Route>();
      const MusECore::Route dst = item->data(ROUTE_DST_COL, RouteDialog::RouteRole).value<MusECore::Route>();
      
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
// Special: Allow simulated midi track to midi port route (a route found in our 'local' routelist
//           but not in any track or port routelist) until multiple output routes are allowed
//           instead of just single port and channel properties. The route is exclusive.
      switch(src.type)
      {
        case MusECore::Route::TRACK_ROUTE:
          switch(dst.type)
          {
            case MusECore::Route::MIDI_PORT_ROUTE:
              if(src.track->isMidiTrack())
              {
                MusECore::MidiTrack* mt = static_cast<MusECore::MidiTrack*>(src.track);
                // We cannot 'remove' a simulated midi track output port and channel route.
                // (Midi port cannot be -1 meaning 'no port'.)
                // Only remove it if it's a different port or channel. 
                if(mt->outPort() != dst.midiPort || mt->outChannel() != src.channel)
                  items_to_remove.append(item);
                continue;
              }
            break;
            
            case MusECore::Route::TRACK_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
              break;
          }
        break;
        
        case MusECore::Route::MIDI_PORT_ROUTE: case MusECore::Route::MIDI_DEVICE_ROUTE: case MusECore::Route::JACK_ROUTE:
        break;
      }
#endif

      if(!MusECore::routeCanDisconnect(src, dst))
        items_to_remove.append(item);
    }
  }
}

void RouteDialog::selectRoutes(bool doNormalSelections)
{
  const QList<QTreeWidgetItem*> route_list = routeList->selectedItems();
  newSrcList->selectRoutes(route_list, doNormalSelections);
  newDstList->selectRoutes(route_list, doNormalSelections);
}  

// bool RouteDialog::routeNodeExists(const MusECore::Route& r)
// {
//   switch(r.type)
//   {
//     case MusECore::Route::TRACK_ROUTE:
//     {
//       MusECore::TrackList* tl = MusEGlobal::song->tracks();
//       for(MusECore::ciTrack i = tl->begin(); i != tl->end(); ++i) 
//       {
//             if((*i)->isMidiTrack())
//               continue;
//             MusECore::AudioTrack* track = (MusECore::AudioTrack*)(*i);
//             if(track->type() == MusECore::Track::AUDIO_INPUT) 
//             {
//               if(r == MusECore::Route(track, -1))
//                 return true;
//               for(int channel = 0; channel < track->channels(); ++channel)
//                 if(r == MusECore::Route(track, channel))
//                   return true;
//               
// //               const MusECore::RouteList* rl = track->inRoutes();
// //               for (MusECore::ciRoute r = rl->begin(); r != rl->end(); ++r) {
// //                     //MusECore::Route dst(track->name(), true, r->channel);
// //                     QString src(r->name());
// //                     if(r->channel != -1)
// //                       src += QString(":") + QString::number(r->channel);
// //                     MusECore::Route dst(track->name(), true, r->channel, MusECore::Route::TRACK_ROUTE);
// //                     item = new QTreeWidgetItem(routeList, QStringList() << src << dst.name());
// //                     item->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(*r));
// //                     item->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
// //                     }
//             }
//             else if(track->type() != MusECore::Track::AUDIO_AUX)
//             {
//               if(r == MusECore::Route(track, -1))
//                 return true;
//             }
//             
//             if(track->type() == MusECore::Track::AUDIO_OUTPUT) 
//             {
//               if(r == MusECore::Route(track, -1))
//                 return true;
//               for (int channel = 0; channel < track->channels(); ++channel) 
//                 if(r == MusECore::Route(track, channel))
//                   return true;
//             }
//             else if(r == MusECore::Route(track, -1))
//               return true;
// 
//     //         const MusECore::RouteList* rl = track->outRoutes();
//     //         for (MusECore::ciRoute r = rl->begin(); r != rl->end(); ++r) 
//     //         {
//     //               QString srcName(track->name());
//     //               if (track->type() == MusECore::Track::AUDIO_OUTPUT) {
//     //                     MusECore::Route s(srcName, false, r->channel);
//     //                     srcName = s.name();
//     //                     }
//     //               if(r->channel != -1)
//     //                 srcName += QString(":") + QString::number(r->channel);
//     //               MusECore::Route src(track->name(), false, r->channel, MusECore::Route::TRACK_ROUTE);
//     //               item = new QTreeWidgetItem(routeList, QStringList() << srcName << r->name());
//     //               item->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
//     //               item->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(*r));
//     //         }
//       }
//     }
//     break;
//     
//     case MusECore::Route::JACK_ROUTE:
//     {
//       if(MusEGlobal::checkAudioDevice())
//       {
//         for(std::list<QString>::iterator i = tmpJackOutPorts.begin(); i != tmpJackOutPorts.end(); ++i)
//           if(r == MusECore::Route(*i, false, -1, MusECore::Route::JACK_ROUTE))
//             return true;
//         for (std::list<QString>::iterator i = tmpJackInPorts.begin(); i != tmpJackInPorts.end(); ++i)
//           if(r == MusECore::Route(*i, true, -1, MusECore::Route::JACK_ROUTE))
//             return true;
//         for(std::list<QString>::iterator i = tmpJackMidiOutPorts.begin(); i != tmpJackMidiOutPorts.end(); ++i)
//           if(r == MusECore::Route(*i, false, -1, MusECore::Route::JACK_ROUTE))
//             return true;
//         for (std::list<QString>::iterator i = tmpJackMidiInPorts.begin(); i != tmpJackMidiInPorts.end(); ++i)
//           if(r == MusECore::Route(*i, true, -1, MusECore::Route::JACK_ROUTE))
//             return true;
//       }
//     }
//     break;
//     
//     case MusECore::Route::MIDI_DEVICE_ROUTE:
//       for(MusECore::iMidiDevice i = MusEGlobal::midiDevices.begin(); i != MusEGlobal::midiDevices.end(); ++i) 
//       {
//         MusECore::MidiDevice* md = *i;
//         // Synth are tracks and devices. Don't list them as devices here, list them as tracks, above.
//         if(md->deviceType() == MusECore::MidiDevice::SYNTH_MIDI)
//           continue;
//         
//         if(r == MusECore::Route(md, -1))
//           return true;
//         for(int channel = 0; channel < MIDI_CHANNELS; ++channel)
//           if(r == MusECore::Route(md, channel))
//             return true;
//       }
//       
//     case MusECore::Route::MIDI_PORT_ROUTE:
//       break;
//       
//   }
//   return false;
// }

void RouteDialog::addItems()
{
  RouteTreeWidgetItem* srcCatItem;
  RouteTreeWidgetItem* dstCatItem;
  RouteTreeWidgetItem* item;
  RouteTreeWidgetItem* subitem;
  QTreeWidgetItem* routesItem;
  // Tried wrap flags: Doesn't work (at least not automatically).
  //const int align_flags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere;
  const int align_flags = Qt::AlignLeft | Qt::AlignVCenter;
  
  //
  // Tracks:
  //
  
  dstCatItem = newDstList->findCategoryItem(tracksCat);
  srcCatItem = newSrcList->findCategoryItem(tracksCat);
  MusECore::TrackList* tl = MusEGlobal::song->tracks();
  for(MusECore::ciTrack i = tl->begin(); i != tl->end(); ++i) 
  {
    MusECore::Track* track = *i;
    const MusECore::RouteCapabilitiesStruct rcaps = track->routeCapabilities();
    int src_chans = 0;
    int dst_chans = 0;
    bool src_routable = false;
    bool dst_routable = false;

    switch(track->type())
    {
      case MusECore::Track::AUDIO_INPUT:
        src_chans = rcaps._trackChannels._outChannels;
        dst_chans = rcaps._jackChannels._inChannels;
        src_routable = rcaps._trackChannels._outRoutable;
        dst_routable = rcaps._jackChannels._inRoutable || rcaps._trackChannels._inRoutable; // Support Audio Out to Audio In omni route.
      break;
      case MusECore::Track::AUDIO_OUTPUT:
        src_chans = rcaps._jackChannels._outChannels;
        dst_chans = rcaps._trackChannels._inChannels;
        src_routable = rcaps._jackChannels._outRoutable || rcaps._trackChannels._outRoutable; // Support Audio Out to Audio In omni route.
        dst_routable = rcaps._trackChannels._inRoutable;
      break;
      case MusECore::Track::MIDI:
      case MusECore::Track::DRUM:
      case MusECore::Track::NEW_DRUM:
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
        src_chans = MIDI_CHANNELS;
#else        
        src_chans = rcaps._midiPortChannels._outChannels;
#endif        
        
        dst_chans = rcaps._midiPortChannels._inChannels;
        src_routable = rcaps._midiPortChannels._outRoutable || rcaps._trackChannels._outRoutable; // Support Midi Track to Audio In omni route.
        dst_routable = rcaps._midiPortChannels._inRoutable;
      break;
      case MusECore::Track::WAVE:
      case MusECore::Track::AUDIO_AUX:
      case MusECore::Track::AUDIO_SOFTSYNTH:
      case MusECore::Track::AUDIO_GROUP:
        src_chans = rcaps._trackChannels._outChannels;
        dst_chans = rcaps._trackChannels._inChannels;
        src_routable = rcaps._trackChannels._outRoutable;
        dst_routable = rcaps._trackChannels._inRoutable;
      break;
    }
  
    
//     if((*i)->isMidiTrack())
//     {
//       MusECore::MidiTrack* track = static_cast<MusECore::MidiTrack*>(*i);
//       
//     }
//     else
//     {
//       const MusECore::AudioTrack* track = static_cast<MusECore::AudioTrack*>(*i);

    
      //
      // DESTINATION section:
      //
    
//       if(track->type() != MusECore::Track::AUDIO_AUX)
//       if(track->totalRoutableInputs(MusECore::Route::TRACK_ROUTE) != 0)
//       if(rcaps._trackChannels._inRoutable || rcaps._trackChannels._inChannels != 0)
      if(dst_routable || dst_chans != 0)
      {
        const MusECore::Route r(track, -1);
        item = newDstList->findItem(r, RouteTreeWidgetItem::RouteItem);
        if(item)
        {
          // Update the text.
          item->setText(ROUTE_NAME_COL, track->name());
        }
        else
        {
          if(!dstCatItem)
          {
            newDstList->blockSignals(true);
            //dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << tracksCat << QString() );
            dstCatItem = new RouteTreeWidgetItem(newDstList, QStringList() << tracksCat, RouteTreeWidgetItem::CategoryItem, false);
            dstCatItem->setFlags(Qt::ItemIsEnabled);
            QFont fnt = dstCatItem->font(ROUTE_NAME_COL);
            fnt.setBold(true);
            fnt.setItalic(true);
            //fnt.setPointSize(fnt.pointSize() + 2);
            dstCatItem->setFont(ROUTE_NAME_COL, fnt);
            dstCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
            dstCatItem->setExpanded(true);
            dstCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
            newDstList->blockSignals(false);
          }
          newDstList->blockSignals(true);
          //item = new QTreeWidgetItem(dstCatItem, QStringList() << track->name() << trackLabel );
          item = new RouteTreeWidgetItem(dstCatItem, QStringList() << track->name(), RouteTreeWidgetItem::RouteItem, false, r);
          //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
          item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
          item->setTextAlignment(ROUTE_NAME_COL, align_flags);
          newDstList->blockSignals(false);
          //dstCatItem->setExpanded(true); // REMOVE Tim. For test only.
        }
        if(QPixmap* r_pm = r.icon(false, false))
          item->setIcon(ROUTE_NAME_COL, QIcon(*r_pm));
        
//         if(track->isMidiTrack())
//           if(rcaps._trackChannels._inChannels != 0)
          if(dst_chans != 0)
          {
//             const MusECore::Route sub_r(track, 0);
            const MusECore::Route sub_r(track, 0, track->isMidiTrack() ? -1 : 1);
            subitem = newDstList->findItem(sub_r, RouteTreeWidgetItem::ChannelsItem);
            if(subitem)
            {
              // Update the channel y values.
              //subitem->computeChannelYValues();
              // Update the number of channels.
              subitem->setChannels();
            }
            else
//             if(!subitem)
            {
              newDstList->blockSignals(true);
              item->setExpanded(true);
              //subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel) << QString() );
              //subitem = new QTreeWidgetItem(item, QStringList() << QString() << QString() );
              subitem = new RouteTreeWidgetItem(item, QStringList() << QString(), RouteTreeWidgetItem::ChannelsItem, false, sub_r);
              //subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(sub_r));
              //subitem->setData(ROUTE_NAME_COL, RouteDialog::ChannelsRole, QVariant::fromValue(QBitArray(MIDI_CHANNELS)));
              subitem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
              subitem->setTextAlignment(ROUTE_NAME_COL, align_flags);
              newDstList->blockSignals(false);
            }
            // Update the channel y values.
            //subitem->computeChannelYValues();
          }
//         }
//         else
//         {
// //           MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(track);
// //           const int chans = atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH ? atrack->totalInChannels() : atrack->channels();
//           //const int chans = atrack->totalRoutableInputs();
//           //for(int channel = 0; channel < chans; ++channel)
// //           if(chans != 0)
// //           {
//             //const MusECore::Route sub_r(track, channel, 1);
//             const MusECore::Route sub_r(track, 0, 1);
//             subitem = newDstList->findItem(sub_r, RouteTreeWidgetItem::ChannelsItem);
// //             if(subitem)
// //             {
// //               // Update the channel y values.
// //               subitem->computeChannelYValues();
// //             }
// //             else
//             if(!subitem)
//             {
//               newDstList->blockSignals(true);
//               item->setExpanded(true);
//               //subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel) << QString() );
//               //subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel) );
//               subitem = new RouteTreeWidgetItem(item, QStringList() << QString(), RouteTreeWidgetItem::ChannelsItem, false, sub_r);
//               //subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(sub_r));
//               //subitem->setData(ROUTE_NAME_COL, RouteDialog::ChannelsRole, QVariant::fromValue(QBitArray(chans)));
//               subitem->setTextAlignment(ROUTE_NAME_COL, align_flags);
//               newDstList->blockSignals(false);
//             }
//             // Update the channel y values.
//             //subitem->computeChannelYValues();
// //           }
//         }
      }

      const MusECore::RouteList* irl = track->inRoutes();
      for(MusECore::ciRoute r = irl->begin(); r != irl->end(); ++r) 
      {
        // Ex. Params:  src: TrackA, Channel  2, Remote Channel -1   dst: TrackB channel  4 Remote Channel -1
        //      After: [src  TrackA, Channel  4, Remote Channel  2]  dst: TrackB channel  2 Remote Channel  4
        //
        // Ex.
        //     Params:  src: TrackA, Channel  2, Remote Channel -1   dst: JackAA channel -1 Remote Channel -1
        //      After: (src  TrackA, Channel -1, Remote Channel  2)  dst: JackAA channel  2 Remote Channel -1
        MusECore::Route src;
        MusECore::Route dst;
        QString srcName;
        QString dstName = track->name();
        switch(r->type)
        {
          case MusECore::Route::JACK_ROUTE: 
            //src = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, r->channels, -1, r->persistentJackPortName);
            //src = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, -1, -1, r->persistentJackPortName);
            src = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, -1, -1, -1, r->persistentJackPortName);
            //src = *r;
            //dst = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       r->channels, -1, 0);
            dst = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       1, -1, 0);
            srcName = r->name(MusEGlobal::config.preferredRouteNameOrAlias);
          break;  
          case MusECore::Route::MIDI_DEVICE_ROUTE: 
            continue;
          break;  
          // Midi ports taken care of below...
          case MusECore::Route::MIDI_PORT_ROUTE: 
            continue;
          break;  
            
          /*{
            //continue;  // TODO
//               //src = MusECore::Route(MusECore::Route::MIDI_PORT_ROUTE,  r->midiPort, 0, r->channel, -1, -1, 0);
//             MusECore::MidiDevice* md = MusEGlobal::midiPorts[r->midiPort].device();
            if(r->channel == -1)
            {
//               if(md)
//                 src = MusECore::Route(md);
//               else
                src = MusECore::Route(r->midiPort);
              dst = MusECore::Route(track);
              srcName = r->name();
              break;
            }
            
//             for(int i = 0; i < MIDI_CHANNELS; ++i)
            {
//               int chbits = 1 << i;
//               if(r->channel & chbits)
              {
                //src = MusECore::Route(r->midiPort, r->channel);
                //src = MusECore::Route(r->midiPort, 1 << i);
//                 if(md)
//                   //src = MusECore::Route(md, chbits);
//                   src = MusECore::Route(md);
//                   //src = MusECore::Route(md, r->channel);
//                 else
                  //src = MusECore::Route(r->midiPort, chbits);
                  //src = MusECore::Route(r->midiPort);
                  src = MusECore::Route(r->midiPort, r->channel);
  //               //dst = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       1, -1, 0);
                //dst = MusECore::Route(track, r->channel, 1);
                dst = MusECore::Route(track, r->channel);
//                 dst = MusECore::Route(track, chbits);
                //dst = MusECore::Route(track, i);
                srcName = r->name();
                //if(src.channel != -1)
                //  srcName += QString(" [") + QString::number(i + 1) + QString("]");
//                 dstName = track->name() + QString(" [") + QString::number(i + 1) + QString("]");
                dstName = track->name() + QString(" [") + QString::number(r->channel + 1) + QString("]");
                routesItem = findRoutesItem(src, dst);
                if(routesItem)
                {
                  // Update the text.
                  routesItem->setText(ROUTE_SRC_COL, srcName);
                  routesItem->setText(ROUTE_DST_COL, dstName);
                }
                else
                {
                  routeList->blockSignals(true);
                  routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
                  routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
                  routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
                  routeList->blockSignals(false);
                }
              }
            }
            continue;
          }
          break;  */
          
          case MusECore::Route::TRACK_ROUTE: 
            src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, r->track, r->remoteChannel, r->channels, -1, 0);
            dst = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,    r->channel,       r->channels, -1, 0);
            srcName = r->name();
          break;  
        }

        if(src.channel != -1)
          srcName += QString(" [") + QString::number(src.channel) + QString("]");
        if(dst.channel != -1)
          dstName += QString(" [") + QString::number(dst.channel) + QString("]");
  


//         QString srcName(r->name());
//         if(r->channel != -1)
//           srcName += QString(":") + QString::number(r->channel);
//         
//         
//         MusECore::Route src(*r);
//         if(src.type == MusECore::Route::JACK_ROUTE)
//           src.channel = -1;
//         //const MusECore::Route dst(track->name(), true, r->channel, MusECore::Route::TRACK_ROUTE);
//         const MusECore::Route dst(MusECore::Route::TRACK_ROUTE, -1, track, r->remoteChannel, r->channels, r->channel, 0);
//         
//         
//         src.remoteChannel = src.channel;
//         dst.remoteChannel = dst.channel;
//         const int src_chan = src.channel;
//         src.channel = dst.channel;
//         dst.channel = src_chan;
        
        
        routesItem = findRoutesItem(src, dst);
        if(routesItem)
        {
          // Update the text.
          routesItem->setText(ROUTE_SRC_COL, srcName);
          routesItem->setText(ROUTE_DST_COL, dstName);
        }
        else
        {
          routeList->blockSignals(true);
          routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
          routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
          routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
          routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
          routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
          if(QPixmap* src_pm = src.icon(true, false))
            routesItem->setIcon(ROUTE_SRC_COL, QIcon(*src_pm));
          if(QPixmap* dst_pm = dst.icon(false, false))
            routesItem->setIcon(ROUTE_DST_COL, QIcon(*dst_pm));
          routeList->blockSignals(false);
        }
      }
//       else if(track->type() != MusECore::Track::AUDIO_AUX)
//       {
//         const MusECore::Route r(track, -1);
//         item = findDstItem(r);
//         if(item)
//         {
//           // Update the text.
//           item->setText(ROUTE_NAME_COL, track->name());
//         }
//         else
//         {
//           if(!dstCatItem)
//           {
//             newDstList->blockSignals(true);
//             dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << tracksCat << QString() );
//             //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//             newDstList->blockSignals(false);
//           }
//           newDstList->blockSignals(true);
//           item = new QTreeWidgetItem(dstCatItem, QStringList() << track->name() << trackLabel );
//           item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//           newDstList->blockSignals(false);
//         }
//         //if((*i)->isMidiTrack())        
//         //if(track->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//         {
//           //for(int channel = 0; channel < track->channels(); ++channel)
//           const int chans = track->type() == MusECore::Track::AUDIO_SOFTSYNTH ? track->totalInChannels() : track->channels();
//           for(int channel = 0; channel < chans; ++channel)
//           {
//             const MusECore::Route subr(track, channel, 1);
//             subitem = findDstItem(subr);
//             if(!subitem)
//             {
//               newDstList->blockSignals(true);
//               subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel) << QString() );
//               subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(subr));
//               newDstList->blockSignals(false);
//             }
//           }
//         }
//       }


      //
      // SOURCE section:
      //

      //if(track->type() == MusECore::Track::AUDIO_OUTPUT) 
//       if(track->totalRoutableOutputs(MusECore::Route::TRACK_ROUTE) != 0
//       if(rcaps._trackChannels._outRoutable || rcaps._trackChannels._outChannels != 0
      if(src_routable || src_chans != 0
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
         || track->isMidiTrack()
#endif                
        )
      {
        const MusECore::Route r(track, -1);
        item = newSrcList->findItem(r, RouteTreeWidgetItem::RouteItem);
        if(item)
        {
          // Update the text.
          item->setText(ROUTE_NAME_COL, track->name());
        }
        else
        {
          if(!srcCatItem)
          {
            newSrcList->blockSignals(true);
            //srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << tracksCat << QString() );
            srcCatItem = new RouteTreeWidgetItem(newSrcList, QStringList() << tracksCat, RouteTreeWidgetItem::CategoryItem, true);
            srcCatItem->setFlags(Qt::ItemIsEnabled);
            QFont fnt = srcCatItem->font(ROUTE_NAME_COL);
            fnt.setBold(true);
            fnt.setItalic(true);
            //fnt.setPointSize(fnt.pointSize() + 2);
            srcCatItem->setFont(ROUTE_NAME_COL, fnt);
            srcCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
            srcCatItem->setExpanded(true);
            srcCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
            newSrcList->blockSignals(false);
          }
          newSrcList->blockSignals(true);
          //item = new QTreeWidgetItem(srcCatItem, QStringList() << track->name() << trackLabel );
          item = new RouteTreeWidgetItem(srcCatItem, QStringList() << track->name(), RouteTreeWidgetItem::RouteItem, true, r);
          //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
          item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
          item->setTextAlignment(ROUTE_NAME_COL, align_flags);
          newSrcList->blockSignals(false);
        }
        if(QPixmap* r_pm = r.icon(true, false))
          item->setIcon(ROUTE_NAME_COL, QIcon(*r_pm));
        
//         if(track->isMidiTrack())
//           if(rcaps._trackChannels._outChannels != 0
          if(src_chans != 0
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
             || track->isMidiTrack()
#endif                
            )
          {
          //for(int channel = 0; channel < MIDI_CHANNELS; ++channel)
          //{
//             const MusECore::Route sub_r(track, 0);
            const MusECore::Route sub_r(track, 0, track->isMidiTrack() ? -1 : 1);
            subitem = newSrcList->findItem(sub_r, RouteTreeWidgetItem::ChannelsItem);
            if(subitem)
            {
              // Update the channel y values.
              //subitem->computeChannelYValues();
              // Update the number of channels.
              subitem->setChannels();
            }
            else
//             if(!subitem)
            {
              newSrcList->blockSignals(true);
              item->setExpanded(true);
              //subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel) << QString() );
              //subitem = new QTreeWidgetItem(item, QStringList() << QString() << QString() );
              
              subitem = new RouteTreeWidgetItem(item, QStringList() << QString(), RouteTreeWidgetItem::ChannelsItem, true, sub_r
#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
              , track->isMidiTrack() ? RouteTreeWidgetItem::ExclusiveMode : RouteTreeWidgetItem::NormalMode
#endif                
              );
              
              //subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(sub_r));
              //subitem->setData(ROUTE_NAME_COL, RouteDialog::ChannelsRole, QVariant::fromValue(QBitArray(MIDI_CHANNELS)));
              subitem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
              subitem->setTextAlignment(ROUTE_NAME_COL, align_flags);
              newSrcList->blockSignals(false);
            }
            // Update the channel y values.
            //subitem->computeChannelYValues();
          }
//         }
//         else
//         {
//           MusECore::AudioTrack* atrack = static_cast<MusECore::AudioTrack*>(track);
// //           const int chans = atrack->type() == MusECore::Track::AUDIO_SOFTSYNTH ? atrack->totalOutChannels() : atrack->channels();
//           const int chans = atrack->totalRoutableOutputs();
//           //for(int channel = 0; channel < chans; ++channel)
//           if(chans != 0)
//           {
//             //const MusECore::Route src_r(track, channel, 1);
//             const MusECore::Route src_r(track, 0, 1);
//             subitem = newSrcList->findItem(src_r, RouteTreeWidgetItem::ChannelsItem);
// //             if(subitem)
// //             {
// //               // Update the channel y values.
// //               subitem->computeChannelYValues();
// //             }
// //             else
//             if(!subitem)
//             {
//               newSrcList->blockSignals(true);
//               item->setExpanded(true);
//               //subitem = new QTreeWidgetItem(item, QStringList() << QString("ch ") + QString::number(channel + 1) << QString() );
//               //subitem = new QTreeWidgetItem(item, QStringList() << QString("ch ") + QString::number(channel + 1) );
//               subitem = new RouteTreeWidgetItem(item, QStringList() << QString(), RouteTreeWidgetItem::ChannelsItem, true, src_r);
//               //subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(src_r));
//               //subitem->setData(ROUTE_NAME_COL, RouteDialog::ChannelsRole, QVariant::fromValue(QBitArray(chans)));
//               subitem->setTextAlignment(ROUTE_NAME_COL, align_flags);
//               newSrcList->blockSignals(false);
//             }
//             // Update the channel y values.
//             //subitem->computeChannelYValues();
//           }
//         }
        
//       }
//       else
//       {
//         const MusECore::Route r(track, -1);
//         item = findSrcItem(r);
//         if(item)
//         {
//           // Update the text.
//           item->setText(ROUTE_NAME_COL, track->name());
//         }
//         else
//         {
//           if(!srcCatItem)
//           {
//             newSrcList->blockSignals(true);
//             srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << tracksCat << QString() );
//             //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//             newSrcList->blockSignals(false);
//           }
//           newSrcList->blockSignals(true);
//           item = new QTreeWidgetItem(srcCatItem, QStringList() << track->name() << trackLabel );
//           item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//           newSrcList->blockSignals(false);
//         }
//         
//         //if(track->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//         {
//           //for(int channel = 0; channel < track->channels(); ++channel)
//           const int chans = track->type() == MusECore::Track::AUDIO_SOFTSYNTH ? track->totalOutChannels() : track->channels();
//           for(int channel = 0; channel < chans; ++channel)
//           {
//             const MusECore::Route subr(track, channel, 1);
//             subitem = findSrcItem(subr);
//             if(!subitem)
//             {
//               newSrcList->blockSignals(true);
//               subitem = new QTreeWidgetItem(item, QStringList() << QString("ch ") + QString::number(channel + 1) << QString() );
//               subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(subr));
//               newSrcList->blockSignals(false);
//             }
//           }
//         }
      }

      const MusECore::RouteList* orl = track->outRoutes();
      for(MusECore::ciRoute r = orl->begin(); r != orl->end(); ++r) 
      {
        // Ex. Params:  src: TrackA, Channel  2, Remote Channel -1   dst: TrackB channel  4 Remote Channel -1
        //      After:  src: TrackA, Channel  4, Remote Channel  2  [dst: TrackB channel  2 Remote Channel  4]
        //
        // Ex.
        //     Params:  src: TrackA, Channel  2, Remote Channel -1   dst: JackAA channel -1 Remote Channel -1
        //      After: (src: TrackA, Channel -1, Remote Channel  2)  dst: JackAA channel  2 Remote Channel -1
        MusECore::Route src;
        MusECore::Route dst;
        QString srcName = track->name();
        QString dstName;
        switch(r->type)
        {
          case MusECore::Route::JACK_ROUTE: 
            //src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       r->channels, -1, 0);
            src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       1, -1, 0);
            //dst = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, r->channels, -1, r->persistentJackPortName);
            //dst = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, -1, -1, r->persistentJackPortName);
            dst = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, -1, -1, -1, r->persistentJackPortName);
            //dst = *r;
            dstName = r->name(MusEGlobal::config.preferredRouteNameOrAlias);
          break;  
          case MusECore::Route::MIDI_DEVICE_ROUTE: 
            continue;  // TODO
          break;  
          // Midi ports taken care of below...
          case MusECore::Route::MIDI_PORT_ROUTE: 
            continue;
          break;  
          
          /*{
            //continue;  // TODO
               //src = MusECore::Route(r->midiPort, r->channel);
//             MusECore::MidiDevice* md = MusEGlobal::midiPorts[r->midiPort].device();
            if(r->channel == -1)
            {
              src = MusECore::Route(track);
//               if(md)
//                 dst = MusECore::Route(md);
//               else
                dst = MusECore::Route(r->midiPort);
              dstName = r->name();
              break;
            }
            
//             for(int i = 0; i < MIDI_CHANNELS; ++i)
            {
//               int chbits = 1 << i;
//               if(r->channel & chbits)
              {
                src = MusECore::Route(track, r->channel);
//                 src = MusECore::Route(track, chbits);
                //src = MusECore::Route(track, i);
  //               //dst = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       1, -1, 0);
                //dst = MusECore::Route(track, r->channel, 1);
                //dst = MusECore::Route(track, r->channel);
                //dst = MusECore::Route(r->midiPort, r->channel);
                //dst = MusECore::Route(r->midiPort, 1 << i);
//                 if(md)
//                   //dst = MusECore::Route(md, chbits);
//                   dst = MusECore::Route(md);
//                   //dst = MusECore::Route(md, r->channel);
//                 else
                  //dst = MusECore::Route(r->midiPort, chbits);
                  //dst = MusECore::Route(r->midiPort);
                  dst = MusECore::Route(r->midiPort, r->channel);
//                 srcName = track->name() + QString(" [") + QString::number(i + 1) + QString("]");
                srcName = track->name() + QString(" [") + QString::number(r->channel + 1) + QString("]");
                dstName = r->name();
                //if(dst.channel != -1)
                //  dstName += QString(" [") + QString::number(i + 1) + QString("]");
                routesItem = findRoutesItem(src, dst);
                if(routesItem)
                {
                  // Update the text.
                  routesItem->setText(ROUTE_SRC_COL, srcName);
                  routesItem->setText(ROUTE_DST_COL, dstName);
                }
                else
                {
                  routeList->blockSignals(true);
                  routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
                  routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
                  routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
                  routeList->blockSignals(false);
                }
              }
            }
            continue;  
          }
          break;*/
          
          case MusECore::Route::TRACK_ROUTE: 
            src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track, r->channel, r->channels, -1, 0);
            dst = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, r->track, r->remoteChannel, r->channels, -1, 0);
            dstName = r->name();
          break;  
        }

        if(src.channel != -1)
          srcName += QString(" [") + QString::number(src.channel) + QString("]");
        if(dst.channel != -1)
          dstName += QString(" [") + QString::number(dst.channel) + QString("]");
        
        
        
        
        //QString srcName(track->name());
        //if(track->type() == MusECore::Track::AUDIO_OUTPUT) 
        //{
        //  const MusECore::Route s(srcName, false, r->channel);
        //  srcName = s.name();
        //}
        //if(src->channel != -1)
        //  srcName += QString(":") + QString::number(r->channel);
        //const MusECore::Route src(track->name(), false, r->channel, MusECore::Route::TRACK_ROUTE);
        //const MusECore::Route src(track->name(), false, r->channel, MusECore::Route::TRACK_ROUTE);
        //const MusECore::Route src(MusECore::Route::TRACK_ROUTE, -1, track, r->remoteChannel, r->channels, r->channel, 0);

        //MusECore::Route dst(*r);
        //if(dst.type == MusECore::Route::JACK_ROUTE)
        //  dst.channel = -1;
        routesItem = findRoutesItem(src, dst);
        if(routesItem)
        {
          // Update the text.
          routesItem->setText(ROUTE_SRC_COL, srcName);
          routesItem->setText(ROUTE_DST_COL, dstName);
        }
        else
        {
          routeList->blockSignals(true);
          routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
          routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
          routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
          routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
          routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
          if(QPixmap* src_pm = src.icon(true, false))
            routesItem->setIcon(ROUTE_SRC_COL, QIcon(*src_pm));
          if(QPixmap* dst_pm = dst.icon(false, false))
            routesItem->setIcon(ROUTE_DST_COL, QIcon(*dst_pm));
          routeList->blockSignals(false);
        }
      }
    }
  //}

  
  //
  // MIDI ports:
  //
  
  const QString none_str = tr("<none>");
  dstCatItem = newDstList->findCategoryItem(midiPortsCat);
  srcCatItem = newSrcList->findCategoryItem(midiPortsCat);
  for(int i = 0; i < MIDI_PORTS; ++i) 
  {
    MusECore::MidiPort* mp = &MusEGlobal::midiPorts[i];
    if(!mp)
      continue;
    MusECore::MidiDevice* md = mp->device();
    // Synth are tracks and devices. Don't list them as devices here, list them as tracks, above.
    //if(md && md->deviceType() == MusECore::MidiDevice::SYNTH_MIDI)
    //  continue;
    
    QString mdname;
    mdname = QString::number(i + 1) + QString(":");
    mdname += md ? md->name() : none_str;
      
    //
    // DESTINATION section:
    //


#ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
    bool non_route_found = false;
    // Simulate routes for each midi track's output port and channel properties.
    MusECore::MidiTrackList* tl = MusEGlobal::song->midis();
    for(MusECore::ciMidiTrack imt = tl->begin(); imt != tl->end(); ++imt)
    {
      MusECore::MidiTrack* mt = *imt;
      const int port = mt->outPort();
      const int chan = mt->outChannel();
      if(port != i)
        continue;
      non_route_found = true;
      const MusECore::Route src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, mt, chan, -1, -1, NULL);
      const MusECore::Route dst(i, chan);
      const QString srcName = mt->name() + QString(" [") + QString::number(chan) + QString("]");
      const QString dstName = mdname;
      routesItem = findRoutesItem(src, dst);
      if(routesItem)
      {
        // Update the text.
        routesItem->setText(ROUTE_SRC_COL, srcName);
        routesItem->setText(ROUTE_DST_COL, dstName);
      }
      else
      {
        routeList->blockSignals(true);
        routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
        routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
        routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
        routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
        routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
        if(QPixmap* src_pm = src.icon(true, true))
          routesItem->setIcon(ROUTE_SRC_COL, QIcon(*src_pm));
        if(QPixmap* dst_pm = dst.icon(false, true))
          routesItem->setIcon(ROUTE_DST_COL, QIcon(*dst_pm));
        routeList->blockSignals(false);
      }
    }
#endif  // _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
    
    //if(md->rwFlags() & 0x02) // Readable
//     if(md->rwFlags() & 0x01)   // Writeable
    //if(md->rwFlags() & 0x03) // Both readable and writeable need to be shown
    
    // Show either all midi ports, or only ports that have a device or have input routes.
    if(allMidiPortsButton->isChecked() || md || !mp->inRoutes()->empty()
       #ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
       || non_route_found
       #endif
    )
    {
      const MusECore::Route dst(i, -1);
      item = newDstList->findItem(dst, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, mdname);
      }
      else
      {
        if(!dstCatItem)
        {
          newDstList->blockSignals(true);
          //dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << midiDevicesCat << QString() );
          dstCatItem = new RouteTreeWidgetItem(newDstList, QStringList() << midiPortsCat, RouteTreeWidgetItem::CategoryItem, false);
          dstCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = dstCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          dstCatItem->setFont(ROUTE_NAME_COL, fnt);
          dstCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          dstCatItem->setExpanded(true);
          dstCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          dstCatItem->setIcon(ROUTE_NAME_COL, QIcon(*settings_midiport_softsynthsIcon));
          newDstList->blockSignals(false);
        }
        newDstList->blockSignals(true);
          
        //item = new QTreeWidgetItem(dstCatItem, QStringList() << mdname << midiDeviceLabel );
        item = new RouteTreeWidgetItem(dstCatItem, QStringList() << mdname, RouteTreeWidgetItem::RouteItem, false, dst);
        //item->setFlags(Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newDstList->blockSignals(false);
      }
//       for(int channel = 0; channel < MIDI_CHANNELS; ++channel)
//       {
//         const MusECore::Route sub_r(md, channel);
//         subitem = findDstItem(sub_r);
//         if(!subitem)
//         {
//           newDstList->blockSignals(true);
//           subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel + 1) << QString() );
//           subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(sub_r));
//           
//           QFont fnt(subitem->font(ROUTE_NAME_COL));
//           fnt.setPointSize(4);
//           //DEBUG_PRST_ROUTES(stderr, "point size:%d family:%s\n", fnt.pointSize(), fnt.family().toLatin1().constData());
//           //subitem->font(ROUTE_NAME_COL).setPointSize(2);
//           //subitem->font(ROUTE_TYPE_COL).setPointSize(2);
//           subitem->setFont(ROUTE_NAME_COL, fnt);
//           subitem->setFont(ROUTE_TYPE_COL, fnt);
//           newDstList->blockSignals(false);
//         }
//       }


// #ifdef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
//       // Simulate routes for each midi track's output port and channel properties.
//       MusECore::MidiTrackList* tl = MusEGlobal::song->midis();
//       for(MusECore::ciMidiTrack imt = tl->begin(); imt != tl->end(); ++imt)
//       {
//         MusECore::MidiTrack* mt = *imt;
//         const int port = mt->outPort();
//         const int chan = mt->outChannel();
//         if(port != i)
//           continue;
//         const MusECore::Route src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, mt, chan, -1, -1, NULL);
//         const MusECore::Route dst(i, chan);
//         const QString srcName = mt->name() + QString(" [") + QString::number(chan) + QString("]");
//         const QString dstName = mdname;
//         routesItem = findRoutesItem(src, dst);
//         if(routesItem)
//         {
//           // Update the text.
//           routesItem->setText(ROUTE_SRC_COL, srcName);
//           routesItem->setText(ROUTE_DST_COL, dstName);
//         }
//         else
//         {
//           routeList->blockSignals(true);
//           routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
//           routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
//           routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
//           routeList->blockSignals(false);
//         }
//       }
// #endif  // _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_

      const MusECore::RouteList* rl = mp->inRoutes();
      for(MusECore::ciRoute r = rl->begin(); r != rl->end(); ++r) 
      {
        switch(r->type)
        {
          case MusECore::Route::TRACK_ROUTE: 
            
#ifndef _USE_MIDI_TRACK_SINGLE_OUT_PORT_CHAN_
          {
            if(!r->track || !r->track->isMidiTrack())
              continue;
            
//             const MusECore::Route src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, r->track, r->channel, r->channels, r->remoteChannel, NULL);
            const MusECore::Route& src = *r;
            QString srcName = r->name();
            QString dstName = mdname;
            //const MusECore::Route dst(i, -1);
            const MusECore::Route dst(i, src.channel);

            if(src.channel != -1)
              srcName += QString(" [") + QString::number(src.channel) + QString("]");
//             if(dst.channel != -1)
//               dstName += QString(" [") + QString::number(dst.channel) + QString("]");
            
            routesItem = findRoutesItem(src, dst);
            if(routesItem)
            {
              // Update the text.
              routesItem->setText(ROUTE_SRC_COL, srcName);
              routesItem->setText(ROUTE_DST_COL, dstName);
            }
            else
            {
              routeList->blockSignals(true);
              routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
              routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
              routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
              routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
              routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
              if(QPixmap* src_pm = src.icon(true, true))
                routesItem->setIcon(ROUTE_SRC_COL, QIcon(*src_pm));
              if(QPixmap* dst_pm = dst.icon(false, true))
                routesItem->setIcon(ROUTE_DST_COL, QIcon(*dst_pm));
              routeList->blockSignals(false);
            }
//             if(!r->jackPort)
//               routesItem->setBackground(ROUTE_SRC_COL, routesItem->background(ROUTE_SRC_COL).color().darker());
          }
#endif  // _USE_MIDI_TRACK_OUT_ROUTES_

          break;
          
          case MusECore::Route::JACK_ROUTE: 
          case MusECore::Route::MIDI_PORT_ROUTE: 
          case MusECore::Route::MIDI_DEVICE_ROUTE: 
          break;
        }
      }
    }
//     else if(track->type() != MusECore::Track::AUDIO_AUX)
//     {
//       const MusECore::Route r(track, -1);
//       item = findDstItem(r);
//       if(!item)
//       {
//         if(!dstCatItem)
//         {
//           newDstList->blockSignals(true);
//           dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << QString("Tracks") << QString() );
//           //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//           newDstList->blockSignals(false);
//         }
//         newDstList->blockSignals(true);
//         item = new QTreeWidgetItem(dstCatItem, QStringList() << track->name() << QString("Track") );
//         item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//         newDstList->blockSignals(false);
//       }
//       //if(track->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//       {
//         //for(int channel = 0; channel < track->channels(); ++channel)
//         const int chans = track->type() == MusECore::Track::AUDIO_SOFTSYNTH ? track->totalInChannels() : track->channels();
//         for(int channel = 0; channel < chans; ++channel)
//         {
//           const MusECore::Route subr(track, channel, 1);
//           subitem = findDstItem(subr);
//           if(!subitem)
//           {
//             newDstList->blockSignals(true);
//             subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel) << QString() );
//             subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(subr));
//             newDstList->blockSignals(false);
//           }
//         }
//       }
//     }
    
    //
    // SOURCE section:
    //
    
    //if(md->rwFlags() & 0x01) // Writeable
//     if(md->rwFlags() & 0x02) // Readable
    //if(md->rwFlags() & 0x03) // Both readable and writeable need to be shown
    
    // Show only ports that have a device, or have output routes.
    if(allMidiPortsButton->isChecked() || md || !mp->outRoutes()->empty())
    {
      const MusECore::Route src(i, -1);
      item = newSrcList->findItem(src, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, mdname);
      }
      else
      {
        if(!srcCatItem)
        {
          newSrcList->blockSignals(true);
          //srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << midiDevicesCat << QString() );
          srcCatItem = new RouteTreeWidgetItem(newSrcList, QStringList() << midiPortsCat, RouteTreeWidgetItem::CategoryItem, true);
          srcCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = srcCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          srcCatItem->setFont(ROUTE_NAME_COL, fnt);
          srcCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          srcCatItem->setExpanded(true);
          srcCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          srcCatItem->setIcon(ROUTE_NAME_COL, QIcon(*settings_midiport_softsynthsIcon));
          newSrcList->blockSignals(false);
        }
        newSrcList->blockSignals(true);
        
        //item = new QTreeWidgetItem(srcCatItem, QStringList() << mdname << midiDeviceLabel );
        item = new RouteTreeWidgetItem(srcCatItem, QStringList() << mdname, RouteTreeWidgetItem::RouteItem, true, src);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newSrcList->blockSignals(false);
      }
//       for(int channel = 0; channel < MIDI_CHANNELS; ++channel)
//       {
//         const MusECore::Route src_r(md, channel);
//         subitem = findSrcItem(src_r);
//         if(!subitem)
//         {
//           newSrcList->blockSignals(true);
//           subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel + 1) << QString() );
//           subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(src_r));
//           newSrcList->blockSignals(false);
//         }
//       }
    
//     else
//     {
//       const MusECore::Route r(track, -1);
//       item = findSrcItem(r);
//       if(!item)
//       {
//         if(!srcCatItem)
//         {
//           newSrcList->blockSignals(true);
//           srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << QString("Tracks") << QString() );
//           //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//           newSrcList->blockSignals(false);
//         }
//         newSrcList->blockSignals(true);
//         item = new QTreeWidgetItem(srcCatItem, QStringList() << track->name() << QString("Track") );
//         item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//         newSrcList->blockSignals(false);
//       }
//       
//       //if(track->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//       {
//         //for(int channel = 0; channel < track->channels(); ++channel)
//         const int chans = track->type() == MusECore::Track::AUDIO_SOFTSYNTH ? track->totalOutChannels() : track->channels();
//         for(int channel = 0; channel < chans; ++channel)
//         {
//           const MusECore::Route subr(track, channel, 1);
//           subitem = findSrcItem(subr);
//           if(!subitem)
//           {
//             newSrcList->blockSignals(true);
//             subitem = new QTreeWidgetItem(item, QStringList() << QString("ch ") + QString::number(channel + 1) << QString() );
//             subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(subr));
//             newSrcList->blockSignals(false);
//           }
//         }
//       }
//     }

      const MusECore::RouteList* rl = mp->outRoutes();
      for(MusECore::ciRoute r = rl->begin(); r != rl->end(); ++r) 
      {
        // Ex. Params:  src: TrackA, Channel  2, Remote Channel -1   dst: TrackB channel  4 Remote Channel -1
        //      After:  src: TrackA, Channel  4, Remote Channel  2  [dst: TrackB channel  2 Remote Channel  4]
        //
        // Ex.
        //     Params:  src: TrackA, Channel  2, Remote Channel -1   dst: JackAA channel -1 Remote Channel -1
        //      After: (src: TrackA, Channel -1, Remote Channel  2)  dst: JackAA channel  2 Remote Channel -1
        //MusECore::Route src(md, -1);
        //MusECore::Route dst;
        //QString srcName = mdname;
        //QString dstName;
        switch(r->type)
        {
          case MusECore::Route::TRACK_ROUTE: 
          {
            if(!r->track || !r->track->isMidiTrack())
              continue;
            
            //const MusECore::Route dst = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, -1, -1, -1, r->persistentJackPortName);
            const MusECore::Route& dst = *r;
            QString dstName = r->name();
            QString srcName = mdname;
            //const MusECore::Route src(i, -1);
            const MusECore::Route src(i, dst.channel);

            //if(src.channel != -1)
            //  srcName += QString(" [") + QString::number(src.channel) + QString("]");
            if(dst.channel != -1)
              dstName += QString(" [") + QString::number(dst.channel) + QString("]");
            
            routesItem = findRoutesItem(src, dst);
            if(routesItem)
            {
              // Update the text.
              routesItem->setText(ROUTE_SRC_COL, srcName);
              routesItem->setText(ROUTE_DST_COL, dstName);
            }
            else
            {
              routeList->blockSignals(true);
              routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
              routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
              routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
              routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
              routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
              if(QPixmap* src_pm = src.icon(true, true))
                routesItem->setIcon(ROUTE_SRC_COL, QIcon(*src_pm));
              if(QPixmap* dst_pm = dst.icon(false, true))
                routesItem->setIcon(ROUTE_DST_COL, QIcon(*dst_pm));
              routeList->blockSignals(false);
            }
//             if(!r->jackPort)
//               routesItem->setBackground(ROUTE_DST_COL, routesItem->background(ROUTE_DST_COL).color().darker());
          }
          break;
          
          case MusECore::Route::JACK_ROUTE:
          case MusECore::Route::MIDI_DEVICE_ROUTE: 
          case MusECore::Route::MIDI_PORT_ROUTE: 
            continue;
        }

  //       QString srcName = mdname;
  //       MusECore::Route src(md, -1);
  // 
  //       if(src.channel != -1)
  //         srcName += QString(" [") + QString::number(src.channel) + QString("]");
  //       if(dst.channel != -1)
  //         dstName += QString(" [") + QString::number(dst.channel) + QString("]");
        
        
        
        
        //QString srcName(track->name());
        //if(track->type() == MusECore::Track::AUDIO_OUTPUT) 
        //{
        //  const MusECore::Route s(srcName, false, r->channel);
        //  srcName = s.name();
        //}
        //if(src->channel != -1)
        //  srcName += QString(":") + QString::number(r->channel);
        //const MusECore::Route src(track->name(), false, r->channel, MusECore::Route::TRACK_ROUTE);
        //const MusECore::Route src(track->name(), false, r->channel, MusECore::Route::TRACK_ROUTE);
        //const MusECore::Route src(MusECore::Route::TRACK_ROUTE, -1, track, r->remoteChannel, r->channels, r->channel, 0);

        //MusECore::Route dst(*r);
        //if(dst.type == MusECore::Route::JACK_ROUTE)
        //  dst.channel = -1;
  //       routesItem = findRoutesItem(src, dst);
  //       if(routesItem)
  //       {
  //         // Update the text.
  //         routesItem->setText(ROUTE_SRC_COL, srcName);
  //         routesItem->setText(ROUTE_DST_COL, dstName);
  //       }
  //       else
  //       {
  //         routeList->blockSignals(true);
  //         routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
  //         routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
  //         routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
  //         routeList->blockSignals(false);
  //       }
      }
    }
  }

  
  //
  // MIDI devices:
  //
  
  dstCatItem = newDstList->findCategoryItem(midiDevicesCat);
  srcCatItem = newSrcList->findCategoryItem(midiDevicesCat);
  for(MusECore::iMidiDevice i = MusEGlobal::midiDevices.begin(); i != MusEGlobal::midiDevices.end(); ++i) 
  {
    MusECore::MidiDevice* md = *i;
    // Synth are tracks and devices. Don't list them as devices here, list them as tracks, above.
    if(md->deviceType() == MusECore::MidiDevice::SYNTH_MIDI)
      continue;
    
//     QString mdname;
//     if(md->midiPort() != -1)
//       mdname = QString::number(md->midiPort() + 1) + QString(":");
//     mdname += md->name();
    QString mdname = md->name();
    //
    // DESTINATION section:
    //
    
    //if(md->rwFlags() & 0x02) // Readable
    //if(md->rwFlags() & 0x01)   // Writeable
    if(md->rwFlags() & 0x03) // Both readable and writeable need to be shown
    {
      const MusECore::Route dst(md, -1);
      item = newDstList->findItem(dst, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, mdname);
      }
      else
      {
        if(!dstCatItem)
        {
          newDstList->blockSignals(true);
          //dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << midiDevicesCat << QString() );
          dstCatItem = new RouteTreeWidgetItem(newDstList, QStringList() << midiDevicesCat, RouteTreeWidgetItem::CategoryItem, false);
          dstCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = dstCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          dstCatItem->setFont(ROUTE_NAME_COL, fnt);
          dstCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          dstCatItem->setExpanded(true);
          dstCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          newDstList->blockSignals(false);
        }
        newDstList->blockSignals(true);
          
        //item = new QTreeWidgetItem(dstCatItem, QStringList() << mdname << midiDeviceLabel );
        item = new RouteTreeWidgetItem(dstCatItem, QStringList() << mdname, RouteTreeWidgetItem::RouteItem, false, dst);
        //item->setFlags(Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newDstList->blockSignals(false);
      }
//       for(int channel = 0; channel < MIDI_CHANNELS; ++channel)
//       {
//         const MusECore::Route sub_r(md, channel);
//         subitem = findDstItem(sub_r);
//         if(!subitem)
//         {
//           newDstList->blockSignals(true);
//           subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel + 1) << QString() );
//           subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(sub_r));
//           
//           QFont fnt(subitem->font(ROUTE_NAME_COL));
//           fnt.setPointSize(4);
//           //DEBUG_PRST_ROUTES(stderr, "point size:%d family:%s\n", fnt.pointSize(), fnt.family().toLatin1().constData());
//           //subitem->font(ROUTE_NAME_COL).setPointSize(2);
//           //subitem->font(ROUTE_TYPE_COL).setPointSize(2);
//           subitem->setFont(ROUTE_NAME_COL, fnt);
//           subitem->setFont(ROUTE_TYPE_COL, fnt);
//           newDstList->blockSignals(false);
//         }
//       }

      const MusECore::RouteList* rl = md->inRoutes();
      for(MusECore::ciRoute r = rl->begin(); r != rl->end(); ++r) 
      {
        // Ex. Params:  src: TrackA, Channel  2, Remote Channel -1   dst: TrackB channel  4 Remote Channel -1
        //      After: [src  TrackA, Channel  4, Remote Channel  2]  dst: TrackB channel  2 Remote Channel  4
        //
        // Ex.
        //     Params:  src: TrackA, Channel  2, Remote Channel -1   dst: JackAA channel -1 Remote Channel -1
        //      After: (src  TrackA, Channel -1, Remote Channel  2)  dst: JackAA channel  2 Remote Channel -1
        //MusECore::Route src;
        //MusECore::Route dst(md, -1);
        //QString srcName;
        //QString dstName = mdname;
        switch(r->type)
        {
          case MusECore::Route::JACK_ROUTE: 
          {
            //src = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, r->channels, -1, r->persistentJackPortName);
            //src = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, -1, -1, r->persistentJackPortName);
            const MusECore::Route src = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, -1, -1, -1, r->persistentJackPortName);
            //src = *r;
            //dst = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       r->channels, -1, 0);
            //dst = MusECore::Route(MusECore::Route::MIDI_DEVICE_ROUTE, -1, md,       r->channel,       1, -1, 0);
            //dst = MusECore::Route(md, r->channel);
            //dst = MusECore::Route(md, -1);
            QString srcName = r->name(MusEGlobal::config.preferredRouteNameOrAlias);
            QString dstName = mdname;
            const MusECore::Route dst(md, -1);

            if(src.channel != -1)
              srcName += QString(" [") + QString::number(src.channel) + QString("]");
            if(dst.channel != -1)
              dstName += QString(" [") + QString::number(dst.channel) + QString("]");
            
            routesItem = findRoutesItem(src, dst);
            if(routesItem)
            {
              // Update the text.
              routesItem->setText(ROUTE_SRC_COL, srcName);
              routesItem->setText(ROUTE_DST_COL, dstName);
            }
            else
            {
              routeList->blockSignals(true);
              routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
              routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
              routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
              routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
              routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
              if(QPixmap* src_pm = src.icon(true, true))
                routesItem->setIcon(ROUTE_SRC_COL, QIcon(*src_pm));
              if(QPixmap* dst_pm = dst.icon(false, true))
                routesItem->setIcon(ROUTE_DST_COL, QIcon(*dst_pm));
              routeList->blockSignals(false);
            }

            QBrush br;
            if(r->jackPort)
            {
              if(routeList->alternatingRowColors())
              {
                const int idx = routeList->indexOfTopLevelItem(routesItem);
                br = (idx != -1 && (idx & 0x01)) ? routeList->palette().alternateBase() : routeList->palette().base();
              }
              else
                br = routeList->palette().base();
              
              routesItem->setBackground(ROUTE_SRC_COL, br);
              routesItem->setForeground(ROUTE_SRC_COL, routeList->palette().windowText());
            }
            else
            {
              //QPalette pal(QColor(Qt::red));
//               if(routeList->alternatingRowColors())
//               {
//                 const int idx = routeList->indexOfTopLevelItem(routesItem);
//                 //br = (idx != -1 && (idx & 0x01)) ? pal.alternateBase() : pal.base();
//                 br = (idx != -1 && (idx & 0x01)) ? QBrush(QColor(Qt::red).darker()) : QBrush(Qt::red);
//               }
//               else
                br = QBrush(Qt::red);
              
              routesItem->setBackground(ROUTE_SRC_COL, br);
              //routesItem->setForeground(ROUTE_SRC_COL, pal.windowText());
            }
          }
          break;
          
          case MusECore::Route::MIDI_DEVICE_ROUTE: 
          case MusECore::Route::MIDI_PORT_ROUTE: 
          case MusECore::Route::TRACK_ROUTE: 
            continue;
        }
        
//         QString dstName = mdname;
//         MusECore::Route dst(md, -1);
// 
//         if(src.channel != -1)
//           srcName += QString(" [") + QString::number(src.channel) + QString("]");
//         if(dst.channel != -1)
//           dstName += QString(" [") + QString::number(dst.channel) + QString("]");
  


//         QString srcName(r->name());
//         if(r->channel != -1)
//           srcName += QString(":") + QString::number(r->channel);
//         
//         
//         MusECore::Route src(*r);
//         if(src.type == MusECore::Route::JACK_ROUTE)
//           src.channel = -1;
//         //const MusECore::Route dst(track->name(), true, r->channel, MusECore::Route::TRACK_ROUTE);
//         const MusECore::Route dst(MusECore::Route::TRACK_ROUTE, -1, track, r->remoteChannel, r->channels, r->channel, 0);
//         
//         
//         src.remoteChannel = src.channel;
//         dst.remoteChannel = dst.channel;
//         const int src_chan = src.channel;
//         src.channel = dst.channel;
//         dst.channel = src_chan;
        
        
//         routesItem = findRoutesItem(src, dst);
//         if(routesItem)
//         {
//           // Update the text.
//           routesItem->setText(ROUTE_SRC_COL, srcName);
//           routesItem->setText(ROUTE_DST_COL, dstName);
//         }
//         else
//         {
//           routeList->blockSignals(true);
//           routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
//           routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
//           routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
//           routeList->blockSignals(false);
//         }
      }
    }
//     else if(track->type() != MusECore::Track::AUDIO_AUX)
//     {
//       const MusECore::Route r(track, -1);
//       item = findDstItem(r);
//       if(!item)
//       {
//         if(!dstCatItem)
//         {
//           newDstList->blockSignals(true);
//           dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << QString("Tracks") << QString() );
//           //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//           newDstList->blockSignals(false);
//         }
//         newDstList->blockSignals(true);
//         item = new QTreeWidgetItem(dstCatItem, QStringList() << track->name() << QString("Track") );
//         item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//         newDstList->blockSignals(false);
//       }
//       //if(track->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//       {
//         //for(int channel = 0; channel < track->channels(); ++channel)
//         const int chans = track->type() == MusECore::Track::AUDIO_SOFTSYNTH ? track->totalInChannels() : track->channels();
//         for(int channel = 0; channel < chans; ++channel)
//         {
//           const MusECore::Route subr(track, channel, 1);
//           subitem = findDstItem(subr);
//           if(!subitem)
//           {
//             newDstList->blockSignals(true);
//             subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel) << QString() );
//             subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(subr));
//             newDstList->blockSignals(false);
//           }
//         }
//       }
//     }
    
    //
    // SOURCE section:
    //
    
    //if(md->rwFlags() & 0x01) // Writeable
    //if(md->rwFlags() & 0x02) // Readable
    if(md->rwFlags() & 0x03) // Both readable and writeable need to be shown
    {
      const MusECore::Route src(md, -1);
      item = newSrcList->findItem(src, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, mdname);
      }
      else
      {
        if(!srcCatItem)
        {
          newSrcList->blockSignals(true);
          //srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << midiDevicesCat << QString() );
          srcCatItem = new RouteTreeWidgetItem(newSrcList, QStringList() << midiDevicesCat, RouteTreeWidgetItem::CategoryItem, true);
          srcCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = srcCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          srcCatItem->setFont(ROUTE_NAME_COL, fnt);
          srcCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          srcCatItem->setExpanded(true);
          srcCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          newSrcList->blockSignals(false);
        }
        newSrcList->blockSignals(true);
        //item = new QTreeWidgetItem(srcCatItem, QStringList() << mdname << midiDeviceLabel );
        item = new RouteTreeWidgetItem(srcCatItem, QStringList() << mdname, RouteTreeWidgetItem::RouteItem, true, src);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newSrcList->blockSignals(false);
      }
//       for(int channel = 0; channel < MIDI_CHANNELS; ++channel)
//       {
//         const MusECore::Route src_r(md, channel);
//         subitem = findSrcItem(src_r);
//         if(!subitem)
//         {
//           newSrcList->blockSignals(true);
//           subitem = new QTreeWidgetItem(item, QStringList() << QString::number(channel + 1) << QString() );
//           subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(src_r));
//           newSrcList->blockSignals(false);
//         }
//       }
    
//     else
//     {
//       const MusECore::Route r(track, -1);
//       item = findSrcItem(r);
//       if(!item)
//       {
//         if(!srcCatItem)
//         {
//           newSrcList->blockSignals(true);
//           srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << QString("Tracks") << QString() );
//           //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//           newSrcList->blockSignals(false);
//         }
//         newSrcList->blockSignals(true);
//         item = new QTreeWidgetItem(srcCatItem, QStringList() << track->name() << QString("Track") );
//         item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(r));
//         newSrcList->blockSignals(false);
//       }
//       
//       //if(track->type() == MusECore::Track::AUDIO_SOFTSYNTH)
//       {
//         //for(int channel = 0; channel < track->channels(); ++channel)
//         const int chans = track->type() == MusECore::Track::AUDIO_SOFTSYNTH ? track->totalOutChannels() : track->channels();
//         for(int channel = 0; channel < chans; ++channel)
//         {
//           const MusECore::Route subr(track, channel, 1);
//           subitem = findSrcItem(subr);
//           if(!subitem)
//           {
//             newSrcList->blockSignals(true);
//             subitem = new QTreeWidgetItem(item, QStringList() << QString("ch ") + QString::number(channel + 1) << QString() );
//             subitem->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(subr));
//             newSrcList->blockSignals(false);
//           }
//         }
//       }
//     }

      const MusECore::RouteList* rl = md->outRoutes();
      for(MusECore::ciRoute r = rl->begin(); r != rl->end(); ++r) 
      {
        // Ex. Params:  src: TrackA, Channel  2, Remote Channel -1   dst: TrackB channel  4 Remote Channel -1
        //      After:  src: TrackA, Channel  4, Remote Channel  2  [dst: TrackB channel  2 Remote Channel  4]
        //
        // Ex.
        //     Params:  src: TrackA, Channel  2, Remote Channel -1   dst: JackAA channel -1 Remote Channel -1
        //      After: (src: TrackA, Channel -1, Remote Channel  2)  dst: JackAA channel  2 Remote Channel -1
        //MusECore::Route src(md, -1);
        //MusECore::Route dst;
        //QString srcName = mdname;
        //QString dstName;
        switch(r->type)
        {
          case MusECore::Route::JACK_ROUTE:
          {
            //src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       r->channels, -1, 0);
            //src = MusECore::Route(MusECore::Route::TRACK_ROUTE, -1, track,       r->channel,       1, -1, 0);
            //src = MusECore::Route(md, r->channel);
            //src = MusECore::Route(md, -1);
            //dst = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, r->channels, -1, r->persistentJackPortName);
            //dst = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, r->remoteChannel, -1, -1, r->persistentJackPortName);
            const MusECore::Route dst = MusECore::Route(MusECore::Route::JACK_ROUTE,  -1, r->jackPort, -1, -1, -1, r->persistentJackPortName);
            //dst = *r;
            QString dstName = r->name(MusEGlobal::config.preferredRouteNameOrAlias);
            QString srcName = mdname;
            const MusECore::Route src(md, -1);

            if(src.channel != -1)
              srcName += QString(" [") + QString::number(src.channel) + QString("]");
            if(dst.channel != -1)
              dstName += QString(" [") + QString::number(dst.channel) + QString("]");
            
            routesItem = findRoutesItem(src, dst);
            if(routesItem)
            {
              // Update the text.
              routesItem->setText(ROUTE_SRC_COL, srcName);
              routesItem->setText(ROUTE_DST_COL, dstName);
            }
            else
            {
              routeList->blockSignals(true);
              routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
              routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
              routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
              routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
              routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
              if(QPixmap* src_pm = src.icon(true, true))
                routesItem->setIcon(ROUTE_SRC_COL, QIcon(*src_pm));
              if(QPixmap* dst_pm = dst.icon(false, true))
                routesItem->setIcon(ROUTE_DST_COL, QIcon(*dst_pm));
              routeList->blockSignals(false);
            }
            
            QBrush br;
            if(r->jackPort)
            {
              if(routeList->alternatingRowColors())
              {
                const int idx = routeList->indexOfTopLevelItem(routesItem);
                br = (idx != -1 && (idx & 0x01)) ? routeList->palette().alternateBase() : routeList->palette().base();
              }
              else
                br = routeList->palette().base();
              
              routesItem->setBackground(ROUTE_DST_COL, br);
              routesItem->setForeground(ROUTE_DST_COL, routeList->palette().windowText());
            }
            else
            {
              //QPalette pal(QColor(Qt::red));
//               if(routeList->alternatingRowColors())
//               {
//                 const int idx = routeList->indexOfTopLevelItem(routesItem);
//                 //br = (idx != -1 && (idx & 0x01)) ? pal.alternateBase() : pal.base();
//                 br = (idx != -1 && (idx & 0x01)) ? QBrush(QColor(Qt::red).darker()) : QBrush(Qt::red);
//               }
//               else
                br = QBrush(Qt::red);
              
              routesItem->setBackground(ROUTE_DST_COL, br);
              //routesItem->setForeground(ROUTE_DST_COL, pal.windowText());
            }
          }
          break;
          
          case MusECore::Route::MIDI_DEVICE_ROUTE: 
          case MusECore::Route::MIDI_PORT_ROUTE: 
          case MusECore::Route::TRACK_ROUTE: 
            continue;
        }

  //       QString srcName = mdname;
  //       MusECore::Route src(md, -1);
  // 
  //       if(src.channel != -1)
  //         srcName += QString(" [") + QString::number(src.channel) + QString("]");
  //       if(dst.channel != -1)
  //         dstName += QString(" [") + QString::number(dst.channel) + QString("]");
        
        
        
        
        //QString srcName(track->name());
        //if(track->type() == MusECore::Track::AUDIO_OUTPUT) 
        //{
        //  const MusECore::Route s(srcName, false, r->channel);
        //  srcName = s.name();
        //}
        //if(src->channel != -1)
        //  srcName += QString(":") + QString::number(r->channel);
        //const MusECore::Route src(track->name(), false, r->channel, MusECore::Route::TRACK_ROUTE);
        //const MusECore::Route src(track->name(), false, r->channel, MusECore::Route::TRACK_ROUTE);
        //const MusECore::Route src(MusECore::Route::TRACK_ROUTE, -1, track, r->remoteChannel, r->channels, r->channel, 0);

        //MusECore::Route dst(*r);
        //if(dst.type == MusECore::Route::JACK_ROUTE)
        //  dst.channel = -1;
  //       routesItem = findRoutesItem(src, dst);
  //       if(routesItem)
  //       {
  //         // Update the text.
  //         routesItem->setText(ROUTE_SRC_COL, srcName);
  //         routesItem->setText(ROUTE_DST_COL, dstName);
  //       }
  //       else
  //       {
  //         routeList->blockSignals(true);
  //         routesItem = new QTreeWidgetItem(routeList, QStringList() << srcName << dstName);
  //         routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(src));
  //         routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(dst));
  //         routeList->blockSignals(false);
  //       }
      }
    }
  }

  //
  // JACK ports:
  //
  
  if(MusEGlobal::checkAudioDevice())
  {
    //------------
    // Jack audio:
    //------------
    
    srcCatItem = newSrcList->findCategoryItem(jackCat);
    MusECore::RouteList in_rl;
    for(std::list<QString>::iterator i = tmpJackOutPorts.begin(); i != tmpJackOutPorts.end(); ++i)
    {
      const MusECore::Route in_r(*i, false, -1, MusECore::Route::JACK_ROUTE);
      item = newSrcList->findItem(in_r, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, in_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
      }
      else
      {
        if(!srcCatItem)
        {
          newSrcList->blockSignals(true);
          //srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << jackCat << QString() );
          srcCatItem = new RouteTreeWidgetItem(newSrcList, QStringList() << jackCat, RouteTreeWidgetItem::CategoryItem, true);
          srcCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = srcCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          srcCatItem->setFont(ROUTE_NAME_COL, fnt);
          srcCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          srcCatItem->setExpanded(true);
          srcCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          srcCatItem->setIcon(ROUTE_NAME_COL, QIcon(*routesInIcon));
          newSrcList->blockSignals(false);
        }
        newSrcList->blockSignals(true);
        //item = new QTreeWidgetItem(srcCatItem, QStringList() << in_r.name() << jackLabel );
        item = new RouteTreeWidgetItem(srcCatItem, 
                                       QStringList() << in_r.name(MusEGlobal::config.preferredRouteNameOrAlias), 
                                       RouteTreeWidgetItem::RouteItem, 
                                       true, 
                                       in_r);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(in_r));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newSrcList->blockSignals(false);
      }
      in_rl.push_back(in_r);
    }

    dstCatItem = newDstList->findCategoryItem(jackCat);
    for(std::list<QString>::iterator i = tmpJackInPorts.begin(); i != tmpJackInPorts.end(); ++i)
    {
      const MusECore::Route out_r(*i, true, -1, MusECore::Route::JACK_ROUTE);
      item = newDstList->findItem(out_r, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, out_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
      }
      else
      {
        if(!dstCatItem)
        {
          newDstList->blockSignals(true);
          //dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << jackCat << QString() );
          dstCatItem = new RouteTreeWidgetItem(newDstList, QStringList() << jackCat, RouteTreeWidgetItem::CategoryItem, false);
          dstCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = dstCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          dstCatItem->setFont(ROUTE_NAME_COL, fnt);
          dstCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          dstCatItem->setExpanded(true);
          dstCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          dstCatItem->setIcon(ROUTE_NAME_COL, QIcon(*routesOutIcon));
          newDstList->blockSignals(false);
        }
        newDstList->blockSignals(true);
        //item = new QTreeWidgetItem(dstCatItem, QStringList() << out_r.name() << jackLabel );
        item = new RouteTreeWidgetItem(dstCatItem, 
                                       QStringList() << out_r.name(MusEGlobal::config.preferredRouteNameOrAlias), 
                                       RouteTreeWidgetItem::RouteItem, 
                                       false, 
                                       out_r);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(out_r));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newDstList->blockSignals(false);
      }
      const QIcon src_ico(*routesInIcon);
      const QIcon dst_ico(*routesOutIcon);
      for(MusECore::ciRoute i = in_rl.begin(); i != in_rl.end(); ++i)
      {
        const MusECore::Route& in_r = *i;
        if(MusECore::routeCanDisconnect(in_r, out_r))
        {
          routesItem = findRoutesItem(in_r, out_r);
          if(routesItem)
          {
            // Update the text.
            routesItem->setText(ROUTE_SRC_COL, in_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
            routesItem->setText(ROUTE_DST_COL, out_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
          }
          else
          {
            routeList->blockSignals(true);
            routesItem = new QTreeWidgetItem(routeList, 
              QStringList() << in_r.name(MusEGlobal::config.preferredRouteNameOrAlias) << out_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
            routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
            routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
            routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(in_r));
            routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(out_r));
            routesItem->setIcon(ROUTE_SRC_COL, src_ico);
            routesItem->setIcon(ROUTE_DST_COL, dst_ico);
            routeList->blockSignals(false);
          }
        }
      }
    }
  
    //------------
    // Jack midi:
    //------------
    
    srcCatItem = newSrcList->findCategoryItem(jackMidiCat);
    in_rl.clear();
    for(std::list<QString>::iterator i = tmpJackMidiOutPorts.begin(); i != tmpJackMidiOutPorts.end(); ++i)
    {
      const MusECore::Route in_r(*i, false, -1, MusECore::Route::JACK_ROUTE);
      item = newSrcList->findItem(in_r, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, in_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
      }
      else
      {
        if(!srcCatItem)
        {
          newSrcList->blockSignals(true);
          //srcCatItem = new QTreeWidgetItem(newSrcList, QStringList() << jackMidiCat << QString() );
          srcCatItem = new RouteTreeWidgetItem(newSrcList, QStringList() << jackMidiCat, RouteTreeWidgetItem::CategoryItem, true);
          srcCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = srcCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          srcCatItem->setFont(ROUTE_NAME_COL, fnt);
          srcCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          srcCatItem->setExpanded(true);
          srcCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          srcCatItem->setIcon(ROUTE_NAME_COL, QIcon(*routesMidiInIcon));
          newSrcList->blockSignals(false);
        }
        newSrcList->blockSignals(true);
        //item = new QTreeWidgetItem(srcCatItem, QStringList() << in_r.name() << jackMidiLabel );
        item = new RouteTreeWidgetItem(srcCatItem, 
                                       QStringList() << in_r.name(MusEGlobal::config.preferredRouteNameOrAlias), 
                                       RouteTreeWidgetItem::RouteItem, 
                                       true, 
                                       in_r);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(in_r));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newSrcList->blockSignals(false);
      }
      in_rl.push_back(in_r);
    }
    
    dstCatItem = newDstList->findCategoryItem(jackMidiCat);
    for(std::list<QString>::iterator i = tmpJackMidiInPorts.begin(); i != tmpJackMidiInPorts.end(); ++i)
    {
      const MusECore::Route out_r(*i, true, -1, MusECore::Route::JACK_ROUTE);
      item = newDstList->findItem(out_r, RouteTreeWidgetItem::RouteItem);
      if(item)
      {
        // Update the text.
        item->setText(ROUTE_NAME_COL, out_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
      }
      else
      {
        if(!dstCatItem)
        {
          newDstList->blockSignals(true);
          //dstCatItem = new QTreeWidgetItem(newDstList, QStringList() << jackMidiCat << QString() );
          dstCatItem = new RouteTreeWidgetItem(newDstList, QStringList() << jackMidiCat, RouteTreeWidgetItem::CategoryItem, false);
          dstCatItem->setFlags(Qt::ItemIsEnabled);
          QFont fnt = dstCatItem->font(ROUTE_NAME_COL);
          fnt.setBold(true);
          fnt.setItalic(true);
          //fnt.setPointSize(fnt.pointSize() + 2);
          dstCatItem->setFont(ROUTE_NAME_COL, fnt);
          dstCatItem->setTextAlignment(ROUTE_NAME_COL, align_flags);
          dstCatItem->setExpanded(true);
          dstCatItem->setBackground(ROUTE_NAME_COL, palette().mid());
          dstCatItem->setIcon(ROUTE_NAME_COL, QIcon(*routesMidiOutIcon));
          newDstList->blockSignals(false);
        }
        newDstList->blockSignals(true);
        //item = new QTreeWidgetItem(dstCatItem, QStringList() << out_r.name() << jackMidiLabel );
        item = new RouteTreeWidgetItem(dstCatItem, 
                                       QStringList() << out_r.name(MusEGlobal::config.preferredRouteNameOrAlias), 
                                       RouteTreeWidgetItem::RouteItem, 
                                       false, 
                                       out_r);
        //item->setData(ROUTE_NAME_COL, RouteDialog::RouteRole, QVariant::fromValue(out_r));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setTextAlignment(ROUTE_NAME_COL, align_flags);
        newDstList->blockSignals(false);
      }
      const QIcon src_ico(*routesMidiInIcon);
      const QIcon dst_ico(*routesMidiOutIcon);
      for(MusECore::ciRoute i = in_rl.begin(); i != in_rl.end(); ++i)
      {
        const MusECore::Route& in_r = *i;
        if(MusECore::routeCanDisconnect(in_r, out_r))
        {
          routesItem = findRoutesItem(in_r, out_r);
          if(routesItem)
          {
            // Update the text.
            routesItem->setText(ROUTE_SRC_COL, in_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
            routesItem->setText(ROUTE_DST_COL, out_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
          }
          else
          {
            routeList->blockSignals(true);
            routesItem = new QTreeWidgetItem(routeList, 
              QStringList() << in_r.name(MusEGlobal::config.preferredRouteNameOrAlias) << out_r.name(MusEGlobal::config.preferredRouteNameOrAlias));
            routesItem->setTextAlignment(ROUTE_SRC_COL, align_flags);
            routesItem->setTextAlignment(ROUTE_DST_COL, align_flags);
            routesItem->setData(ROUTE_SRC_COL, RouteDialog::RouteRole, QVariant::fromValue(in_r));
            routesItem->setData(ROUTE_DST_COL, RouteDialog::RouteRole, QVariant::fromValue(out_r));
            routesItem->setIcon(ROUTE_SRC_COL, src_ico);
            routesItem->setIcon(ROUTE_DST_COL, dst_ico);
            routeList->blockSignals(false);
          }
        }
      }
    }
  }
}

void MusE::startRouteDialog()
{
  if(routeDialog == 0)
    // NOTE: For deleting parentless dialogs and widgets, please add them to MusE::deleteParentlessDialogs().
    routeDialog = new MusEGui::RouteDialog;
  routeDialog->show();
  routeDialog->raise();
}


} // namespace MusEGui
