// Chats: every conversation (people + groups), newest first.
#include "Common.h"
#include <stdio.h>

class ChatsPage : public Page {
public:
  void build(lv_obj_t* parent, int w, int h) override {
    _list.create(parent, 0, 0, w, h);
    _list.onActivate = [this](int i) {
      if (i >= 0 && i < (int)_keys.size()) app->openChat(_keys[i]);
    };
    fill();
  }

  void addChatRow(const ConvKey& k, const char* name, const char* preview, uint32_t t, int unread,
                  int last_dir, int last_status, const DeviceStatus& st) {
    bool group = k.kind == KIND_CHANNEL;
    if (!T::easy()) { compactRow(k, name, preview, t, unread, last_dir, last_status, st); return; }
    lv_obj_t* r = _list.addRow();
    int av = T::avatar;
    lv_obj_t* a = ui::avatar(r, name, k.id, av, group);
    lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
    int x = av + 10;
    int right_w = 0;

    char tb[24] = "";
    if (t && st.clock_valid) ui::relTime(t, st.now, m->tzOffset(), tb, sizeof(tb));
    if (tb[0]) {
      lv_obj_t* tl = ui::label(r, tb, T::caption, unread ? T::accent_text : T::faint);
      lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, 0, T::easy() ? 10 : 8);
      lv_obj_update_layout(tl);
      right_w = lv_obj_get_width(tl) + 6;
    }
    int text_w = SCR_W - 2 * T::pad - 16 - x;
    lv_obj_t* nm = ui::label(r, name, T::body_semi, T::text);
    ui::oneLine(nm, text_w - right_w);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, x, T::easy() ? 8 : 6);

    char pv[96];
    const char* prefix = "";
    if (last_dir == DIR_OUT) {
      prefix = last_status == ST_DELIVERED ? IC_CHECK2 " " : last_status == ST_FAILED ? IC_ERROR " " :
               last_status == ST_PENDING ? IC_CLOCK " " : IC_CHECK " ";
    }
    snprintf(pv, sizeof(pv), "%s%s", prefix, preview && *preview ? preview : "No messages yet");
    lv_obj_t* p = ui::label(r, pv, T::small, unread ? T::text : T::muted);
    ui::oneLine(p, text_w - (unread ? 30 : 0));
    lv_obj_align(p, LV_ALIGN_BOTTOM_LEFT, x, T::easy() ? -8 : -6);
    if (unread) {
      lv_obj_t* b = ui::badge(r, unread);
      lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, T::easy() ? -8 : -6);
    }
    _keys.push_back(k);
  }

  // Dense MeshOS-style row: avatar, name + time, preview + unread
  void compactRow(const ConvKey& k, const char* name, const char* preview, uint32_t t, int unread,
                  int last_dir, int last_status, const DeviceStatus& st) {
    bool group = k.kind == KIND_CHANNEL;
    lv_obj_t* r = _list.addRow(38);
    lv_obj_t* a = ui::avatar(r, name, k.id, 24, group);
    lv_obj_align(a, LV_ALIGN_LEFT_MID, 0, 0);
    const int x = 32, w = SCR_W - 2 * T::pad - 12 - x;
    char tb[24] = "";
    if (t && st.clock_valid) ui::relTime(t, st.now, m->tzOffset(), tb, sizeof(tb));
    int tw = 0;
    if (tb[0]) {
      lv_obj_t* tl = ui::label(r, tb, T::f12, unread ? T::accent_text : T::faint);
      lv_obj_align(tl, LV_ALIGN_TOP_RIGHT, 0, 3);
      lv_obj_update_layout(tl);
      tw = lv_obj_get_width(tl) + 6;
    }
    bool muted = group && m->channelMuted(k.id);
    char nmb[48];
    snprintf(nmb, sizeof(nmb), muted ? "%s " IC_MUTE : "%s", name);
    lv_obj_t* nm = ui::label(r, nmb, T::f14b, T::text);
    ui::oneLine(nm, w - tw);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, x, 2);
    char pv[96];
    const char* prefix = "";
    if (last_dir == DIR_OUT) prefix = last_status == ST_DELIVERED ? IC_CHECK2 " " : last_status == ST_FAILED ? IC_ERROR " " :
                                      last_status == ST_PENDING ? IC_CLOCK " " : IC_CHECK " ";
    snprintf(pv, sizeof(pv), "%s%s", prefix, preview && *preview ? preview : "No messages yet");
    lv_obj_t* p = ui::label(r, pv, T::f12, unread ? T::text : T::muted);
    ui::oneLine(p, w - (unread ? 24 : 0));
    lv_obj_align(p, LV_ALIGN_BOTTOM_LEFT, x, -3);
    if (unread) {
      lv_obj_t* b = ui::badge(r, unread);
      if (muted) lv_obj_set_style_bg_color(b, T::faint, 0);   // muted: quiet grey badge
      lv_obj_set_height(b, 16);
      lv_obj_set_style_min_width(b, 16, 0);
      lv_obj_set_style_pad_hor(b, 4, 0);
      lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
    }
    _keys.push_back(k);
  }

  void fill() {
    int sel = _list.selected();
    _list.clear();
    _keys.clear();
    DeviceStatus st = m->status();

    static ConvSummary sums[40];
    int n = m->listConversations(sums, 40);
    for (int i = 0; i < n; i++) {
      char name[LOG_NAME_MAX + 8];
      if (sums[i].key.kind == KIND_CHANNEL) {
        ChannelView ch;
        snprintf(name, sizeof(name), "%s", m->findChannel(sums[i].key.id, ch) ? ch.name : "Group");
      } else {
        ContactView c;
        snprintf(name, sizeof(name), "%s", m->findContact(sums[i].key.id, c) ? c.name : (sums[i].name[0] ? sums[i].name : "Unknown"));
      }
      addChatRow(sums[i].key, name, sums[i].preview, sums[i].last_time, sums[i].unread, sums[i].last_dir, sums[i].last_status, st);
    }
    // joined groups that have no messages yet
    for (int i = 0; i < m->channelCount(); i++) {
      ChannelView ch;
      if (!m->channelAt(i, ch)) continue;
      ConvKey k = ConvKey::channel(ch.key);
      bool seen = false;
      for (auto& kk : _keys) if (kk == k) { seen = true; break; }
      if (!seen) addChatRow(k, ch.name, nullptr, 0, 0, -1, 0, st);
    }
    if (_keys.empty()) {
      _list.setEmpty(IC_CHATS, "No chats yet", "Go to People to say hi, or join a group in Settings.");
    }
    _list.select(sel, false);
    _list.animate = false;   // only animate the first appearance
  }

  bool onInput(const InputEvent& e) override {
    if (_list.handleNav(e.type)) return true;
    if (e.type == IN_CHAR && e.ch == '/') { app->push(makeSearchScreen()); return true; }   // search messages
    if (e.type == IN_CHAR && e.ch == '$') {   // jump to the next chat with unread messages
      int n = (int)_keys.size();
      for (int k = 1; k <= n; k++) {
        int i = (_list.selected() + k) % n;
        if (m->unreadCount(_keys[i]) > 0) { _list.select(i); return true; }
      }
      app->toast(IC_CHECK2, "No unread messages");
      return true;
    }
    return false;
  }
  void onMessage(const LogRecord& r, bool appended) override { _dirty = true; }
  void onContacts() override { _dirty = true; }
  void onShow() override { if (_dirty) { _dirty = false; fill(); } }
  void tick() override { if (_dirty) { _dirty = false; fill(); } }

private:
  FocusList _list;
  std::vector<ConvKey> _keys;
  bool _dirty = false;
};

Page* makeChatsPage() { return new ChatsPage(); }
