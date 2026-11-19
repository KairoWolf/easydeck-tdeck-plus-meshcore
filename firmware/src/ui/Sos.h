#pragma once
// SOS engine: guarded start, fast-then-slow position updates to SOS contacts
// (favourites) by direct message (ACKed + retried by the mesh layer), optional
// #emergency post, confirmed cancel, and "I'm OK" check-ins.
// Messages are plain text so every MeshCore client can read them:
//   SOS! Alex needs help. -31.91234,152.46301 14:32 bat 41% #3
//   SOS CANCELLED - Alex is OK. -31.91234,152.46301 14:51
//   OK check-in from Alex. -31.91234,152.46301 14:32 bat 80%
//   SOS ACK from Sam - I've seen your SOS.
#include <stdint.h>
#include <vector>
#include "core/Model.h"

class App;

class Sos {
public:
  void begin(App* app, Model* m);
  void loop();                      // ~every main loop pass
  bool active() const;
  void start();                     // call after the arming countdown
  void cancel();
  void checkIn();
  void shareLoop();                 // periodic location to favourites (Settings > GPS)
  static bool isLocText(const char* t);   // "LOC " beacon
  int  seq() const;
  uint32_t startedAt() const;       // unix time
  int  secondsToNext() const;
  int  contacts(std::vector<ContactView>& out);   // SOS recipients
  bool emergencyChannel(ChannelView* out = nullptr);

  static bool isSosText(const char* t);           // "SOS!" prefix
  static bool parseLocation(const char* t, double& lat, double& lon);

private:
  void sendUpdate();
  void broadcast(const char* text, bool to_emergency);
  void position(char* out, int n);
  App* _app = nullptr;
  Model* _m = nullptr;
  uint32_t _next_ms = 0;
  uint32_t _ref_now = 0, _ref_tick = 0;
  uint32_t _share_ms = 0, _share_beat_ms = 0;
  double _share_lat = 0, _share_lon = 0;
};
