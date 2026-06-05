#pragma once

#include "control/controlproxy.h"
#include "skin/legacy/skincontext.h"
#include "track/track.h"

class WStemControlBorder : public WWidgetGroup {
    Q_OBJECT
  public:
    WStemControlBorder(QWidget* pParent);
    ~WStemControlBorder() override = default;

    void setup(const QDomNode& node, const SkinContext& context) override;

    void setStemNumber(int stemNo) {
        m_stemNo = stemNo;
    }

  public slots:
    void slotTrackLoaded(TrackPointer pTrack);
    void slotTrackUnloaded();

  private:
    void updateBorder();
    void setBorderColor(const QColor& color);
    void updateBorderStyle(const QColor& color);

    int m_stemNo{1};
    QString m_group;
    StemInfo m_stemInfo;
};

// #pragma once
//
// #include "track/steminfo.h"
// #include "skin/legacy/skincontext.h"
// #include "widget/wwidgetgroup.h"
//
// class WStemControlBorder : public WWidgetGroup {
//     Q_OBJECT
//   public:
//     explicit WStemControlBorder(QWidget* pParent = nullptr);
//
//     void setup(const QDomNode& node, const SkinContext& context) override;
//
//   public slots:
//     void slotTrackLoaded(TrackPointer pTrack);
//     void slotTrackUnloaded();
//
//   private:
//     void updateBottomLine(const QColor& color);
//
//     StemInfo m_stemInfo;
//     int m_stemNo = 0;
//     QString m_group;
// };
//
////#pragma once
////
////#include "control/controlproxy.h"
////#include "skin/legacy/skincontext.h"
////#include "track/steminfo.h"
////#include "widget/wwidgetgroup.h"
////
////class LegacySkinParser;
////
////class WStemControlBorder : public WWidgetGroup {
////    Q_OBJECT
////  public:
////    explicit WStemControlBorder(QWidget* pParent = nullptr);
////
////    void setup(const QDomNode& node, const SkinContext& context);
////    void setParser(LegacySkinParser* pParser) {
////        m_pParser = pParser;
////    }
////
////  public slots:
////    void slotTrackLoaded(TrackPointer pTrack);
////    void slotTrackUnloaded();
////
////  private:
////    void updateBorderStyle(const QColor& color);
////    void parseChildren(const QDomNode& node, const SkinContext& context);
////    // void parseChildren(const QDomNode& node, const SkinContext& context);
////
////    StemInfo m_stemInfo;
////    int m_stemNo = 0;
////    QString m_group;
////    LegacySkinParser* m_pParser = nullptr;
////};
////
////
//////#pragma once
//////
//////#include "control/controlproxy.h"
//////#include "skin/legacy/skincontext.h"
//////#include "track/track.h"
//////// #include "widget/wlabel.h"
//////
//////class WStemControlBorder : public WWidgetGroup {
//////    Q_OBJECT
//////  public:
//////    WStemControlBorder(QWidget* pParent);
//////    ~WStemControlBorder() override = default;
//////
//////    void setup(const QDomNode& node, const SkinContext& context) override;
//////
//////    void setStemNumber(int stemNo) {
//////        m_stemNo = stemNo;
//////    }
//////
//////  public slots:
//////    void slotTrackLoaded(TrackPointer pTrack);
//////    void slotTrackUnloaded();
//////
//////  private:
//////    void updateBorder();
//////    void setBorderColor(const QColor& color);
//////    void updateBorderStyle(const QColor& color);
//////
//////    int m_stemNo{1};
//////    QString m_group;
//////    StemInfo m_stemInfo;
//////};
//////
//////
////////class WStemControlBorder : public WWidgetGroup {
////////    Q_OBJECT
////////  public:
////////    explicit WStemControlBorder(QWidget* pParent = nullptr);
////////
////////    void setup(const QDomNode& node, const SkinContext& context) override;
////////
////////  public slots:
////////    void slotTrackLoaded(TrackPointer pTrack);
////////    void slotTrackUnloaded(TrackPointer pTrack);
////////
////////  private slots:
////////    void updateBorder();
////////
////////  private:
////////    void setBorderColor(const QColor& color);
////////
////////    StemInfo m_stemInfo;
////////    QString m_group;
////////    int m_stemNo;
////////
////////};
