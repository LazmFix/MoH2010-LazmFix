// Wrappers for IDirectInput8 and IDirectInputDevice8. Included twice by dinput_wrap.cpp, with
// DI_W = 1 (Unicode interfaces) and DI_W = 0 (ANSI interfaces).
//
// IDirectInput8: every call that builds DirectInput's device list (and so opens HID collections)
// runs inside a HidGuard::Scope. CreateDevice wraps the device it returns.
// IDirectInputDevice8: pass-through; for the system mouse it reports the game's setup to the core
// and lets the core process what the game reads.

#if DI_W
#define DI_T(x) x##W
#define DI_STR LPCWSTR
#else
#define DI_T(x) x##A
#define DI_STR LPCSTR
#endif

class DI_T(WrapDevice8) final : public DI_T(IDirectInputDevice8) {
 public:
  DI_T(WrapDevice8)(DI_T(IDirectInputDevice8) * real, bool mouse) : real_(real), mouse_(mouse) {}

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == DI_T(IID_IDirectInputDevice8)) {
      AddRef();
      *object = this;
      return S_OK;
    }
    return real_->QueryInterface(riid, object);
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    refs_.fetch_add(1);
    return real_->AddRef();
  }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = real_->Release();
    if (refs_.fetch_sub(1) == 1) {
      delete this;
    }
    return left;
  }

  // IDirectInputDevice8
  HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override { return real_->GetCapabilities(caps); }
  HRESULT STDMETHODCALLTYPE EnumObjects(DI_T(LPDIENUMDEVICEOBJECTSCALLBACK) callback, LPVOID ref, DWORD flags) override {
    return real_->EnumObjects(callback, ref, flags);
  }
  HRESULT STDMETHODCALLTYPE GetProperty(REFGUID property, LPDIPROPHEADER header) override {
    return real_->GetProperty(property, header);
  }
  HRESULT STDMETHODCALLTYPE SetProperty(REFGUID property, LPCDIPROPHEADER header) override {
    return real_->SetProperty(property, header);
  }
  HRESULT STDMETHODCALLTYPE Acquire() override {
    if (mouse_) {
      window::pump();
    }
    HidGuard::Scope guard;
    const HRESULT hr = real_->Acquire();
    if (mouse_ && SUCCEEDED(hr)) {
      core::mouse_set_acquired(true);
    }
    return hr;
  }
  HRESULT STDMETHODCALLTYPE Unacquire() override {
    const HRESULT hr = real_->Unacquire();
    if (mouse_) {
      core::mouse_set_acquired(false);
    }
    return hr;
  }
  HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override {
    if (mouse_) {
      window::pump();
    }
    const HRESULT hr = real_->GetDeviceState(size, data);
    if (mouse_ && SUCCEEDED(hr)) {
      core::mouse_process_state(size, data);
    }
    return hr;
  }
  HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD object_size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags) override {
    if (mouse_) {
      window::pump();
    }
    const HRESULT hr = real_->GetDeviceData(object_size, data, count, flags);
    if (mouse_ && count != nullptr && (flags & DIGDD_PEEK) == 0) {
      core::mouse_process_data(object_size, data, *count, hr);
    }
    return hr;
  }
  HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT data_format) override {
    const HRESULT hr = real_->SetDataFormat(data_format);
    if (mouse_ && SUCCEEDED(hr)) {
      core::mouse_set_data_format(data_format);
    }
    return hr;
  }
  HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE event) override { return real_->SetEventNotification(event); }
  HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD flags) override {
    const HRESULT hr = real_->SetCooperativeLevel(window, flags);
    if (mouse_ && SUCCEEDED(hr)) {
      core::mouse_set_cooperative_level(window, flags);
    }
    return hr;
  }
  HRESULT STDMETHODCALLTYPE GetObjectInfo(DI_T(LPDIDEVICEOBJECTINSTANCE) info, DWORD object, DWORD how) override {
    return real_->GetObjectInfo(info, object, how);
  }
  HRESULT STDMETHODCALLTYPE GetDeviceInfo(DI_T(LPDIDEVICEINSTANCE) info) override { return real_->GetDeviceInfo(info); }
  HRESULT STDMETHODCALLTYPE RunControlPanel(HWND owner, DWORD flags) override { return real_->RunControlPanel(owner, flags); }
  HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version, REFGUID guid) override {
    HidGuard::Scope guard;
    return real_->Initialize(instance, version, guid);
  }
  HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID guid, LPCDIEFFECT effect, LPDIRECTINPUTEFFECT* out, LPUNKNOWN outer) override {
    return real_->CreateEffect(guid, effect, out, outer);
  }
  HRESULT STDMETHODCALLTYPE EnumEffects(DI_T(LPDIENUMEFFECTSCALLBACK) callback, LPVOID ref, DWORD type) override {
    return real_->EnumEffects(callback, ref, type);
  }
  HRESULT STDMETHODCALLTYPE GetEffectInfo(DI_T(LPDIEFFECTINFO) info, REFGUID guid) override {
    return real_->GetEffectInfo(info, guid);
  }
  HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD out) override { return real_->GetForceFeedbackState(out); }
  HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD flags) override { return real_->SendForceFeedbackCommand(flags); }
  HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback, LPVOID ref,
                                                     DWORD flags) override {
    return real_->EnumCreatedEffectObjects(callback, ref, flags);
  }
  HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE escape) override { return real_->Escape(escape); }
  HRESULT STDMETHODCALLTYPE Poll() override { return real_->Poll(); }
  HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD object_size, LPCDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags) override {
    return real_->SendDeviceData(object_size, data, count, flags);
  }
  HRESULT STDMETHODCALLTYPE EnumEffectsInFile(DI_STR file, LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID ref,
                                              DWORD flags) override {
    return real_->EnumEffectsInFile(file, callback, ref, flags);
  }
  HRESULT STDMETHODCALLTYPE WriteEffectToFile(DI_STR file, DWORD count, LPDIFILEEFFECT effects, DWORD flags) override {
    return real_->WriteEffectToFile(file, count, effects, flags);
  }
  HRESULT STDMETHODCALLTYPE BuildActionMap(DI_T(LPDIACTIONFORMAT) format, DI_STR user, DWORD flags) override {
    return real_->BuildActionMap(format, user, flags);
  }
  HRESULT STDMETHODCALLTYPE SetActionMap(DI_T(LPDIACTIONFORMAT) format, DI_STR user, DWORD flags) override {
    return real_->SetActionMap(format, user, flags);
  }
  HRESULT STDMETHODCALLTYPE GetImageInfo(DI_T(LPDIDEVICEIMAGEINFOHEADER) header) override { return real_->GetImageInfo(header); }

 private:
  ~DI_T(WrapDevice8)() = default;

  DI_T(IDirectInputDevice8) * real_;
  bool mouse_;
  std::atomic<ULONG> refs_{1};
};

class DI_T(WrapDI8) final : public DI_T(IDirectInput8) {
 public:
  explicit DI_T(WrapDI8)(DI_T(IDirectInput8) * real) : real_(real) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == DI_T(IID_IDirectInput8)) {
      AddRef();
      *object = this;
      return S_OK;
    }
    return real_->QueryInterface(riid, object);
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    refs_.fetch_add(1);
    return real_->AddRef();
  }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG left = real_->Release();
    if (refs_.fetch_sub(1) == 1) {
      delete this;
    }
    return left;
  }

  HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, DI_T(LPDIRECTINPUTDEVICE8) * device, LPUNKNOWN outer) override {
    HidGuard::Scope guard;
    const HRESULT hr = real_->CreateDevice(guid, device, outer);
    if (FAILED(hr) || device == nullptr || *device == nullptr || outer != nullptr || !core::is_game_process()) {
      return hr;
    }
    const bool mouse = guid == GUID_SysMouse || guid == GUID_SysMouseEm || guid == GUID_SysMouseEm2;
    if (!mouse) {
      return hr;  // Keyboard and game controllers: untouched.
    }
    *device = new DI_T(WrapDevice8)(*device, true);
    core::mouse_note_created();
    return hr;
  }
  HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type, DI_T(LPDIENUMDEVICESCALLBACK) callback, LPVOID ref, DWORD flags) override {
    HidGuard::Scope guard;
    return real_->EnumDevices(type, callback, ref, flags);
  }
  HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID guid) override {
    HidGuard::Scope guard;
    return real_->GetDeviceStatus(guid);
  }
  HRESULT STDMETHODCALLTYPE RunControlPanel(HWND owner, DWORD flags) override { return real_->RunControlPanel(owner, flags); }
  HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE instance, DWORD version) override {
    HidGuard::Scope guard;
    return real_->Initialize(instance, version);
  }
  HRESULT STDMETHODCALLTYPE FindDevice(REFGUID guid, DI_STR name, LPGUID out) override {
    HidGuard::Scope guard;
    return real_->FindDevice(guid, name, out);
  }
  HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(DI_STR user, DI_T(LPDIACTIONFORMAT) format,
                                                   DI_T(LPDIENUMDEVICESBYSEMANTICSCB) callback, LPVOID ref,
                                                   DWORD flags) override {
    HidGuard::Scope guard;
    return real_->EnumDevicesBySemantics(user, format, callback, ref, flags);
  }
  HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK callback, DI_T(LPDICONFIGUREDEVICESPARAMS) params,
                                             DWORD flags, LPVOID ref) override {
    HidGuard::Scope guard;
    return real_->ConfigureDevices(callback, params, flags, ref);
  }

 private:
  ~DI_T(WrapDI8)() = default;

  DI_T(IDirectInput8) * real_;
  std::atomic<ULONG> refs_{1};
};

#undef DI_T
#undef DI_STR
