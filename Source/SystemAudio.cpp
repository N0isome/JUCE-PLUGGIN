#include "SystemAudio.h"
#include <cmath>
#include <cstring>
#if JUCE_WINDOWS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

namespace systemaudio
{
bool Format::valid() const noexcept
{
    return channels >= 1 && channels <= 32
        && (floatingPoint ? bits == 32 : bits == 8 || bits == 16 || bits == 24 || bits == 32)
        && stride >= channels * (bits / 8);
}
bool decode(const void* bytes, int frames, Format f, float* l, float* r, bool silent) noexcept
{
    if (!f.valid() || frames < 0 || !l || !r || (!silent && !bytes)) return false;
    if (silent) { std::fill_n(l, frames, 0.0f); std::fill_n(r, frames, 0.0f); return true; }
    auto sample = [&f](const unsigned char* p) noexcept
    {
        if (f.floatingPoint)
        {
            float v; std::memcpy(&v, p, sizeof(v)); return std::isfinite(v) ? v : 0.0f;
        }
        if (f.bits == 8) return (static_cast<int>(*p) - 128) / 128.0f;
        const int n = f.bits / 8;
        std::uint32_t value = 0;
        for (int i = 0; i < n; ++i) value |= static_cast<std::uint32_t>(p[i]) << (8 * i);
        // Sign extension expressed without implementation-defined signed shifts.
        const std::int64_t signedValue = (value & (std::uint32_t(1) << (f.bits - 1))) != 0
            ? static_cast<std::int64_t>(value) - (std::int64_t(1) << f.bits) : value;
        return static_cast<float>(static_cast<double>(signedValue) / std::ldexp(1.0, f.bits - 1));
    };
    auto* p = static_cast<const unsigned char*>(bytes);
    for (int i = 0; i < frames; ++i, p += f.stride)
    {
        l[i] = sample(p);
        r[i] = f.channels == 1 ? l[i] : sample(p + f.bits / 8);
    }
    return true;
}

Capture::Capture() : juce::Thread("JUCE PLUGGIN system capture") {}
Capture::~Capture() { stop(); }
bool Capture::supported() noexcept
{
#if JUCE_WINDOWS
    return true;
#else
    return false;
#endif
}
void Capture::setMessage(const juce::String& text, bool error)
{
    const std::lock_guard lock(mutex); message = text; failed.store(error);
}
Capture::Status Capture::status() const
{
    const std::lock_guard lock(mutex);
    return { ready(), failed.load(), message };
}
bool Capture::ready() const noexcept { return consumerGate.state.load() == capture::State::recording; }
void Capture::requestStop() noexcept
{
    consumerGate.state.store(capture::State::idle);
    signalThreadShouldExit();
}
void Capture::stop() { requestStop(); stopThread(-1); }
bool Capture::start(const juce::String& id)
{
    stop(); deviceId = id; failed.store(false);
    if (!supported()) { setMessage("Captura directa disponible en Windows. En macOS usa la entrada del DAW.", true); return false; }
    setMessage("Abriendo salida de Windows...", false);
    if (!startThread()) { setMessage("No se pudo iniciar el hilo de captura.", true); return false; }
    return true;
}
void Capture::render(juce::AudioBuffer<float>& buffer, double hostRate) noexcept
{
    buffer.clear();
    capture::SessionGate::ProducerScope consumer(consumerGate);
    if (!consumer.canCapture() || !std::isfinite(hostRate) || hostRate < 8000 || hostRate > 384000 || buffer.getNumChannels() < 2) return;
    const auto current = generation.load();
    if (consumerGeneration != current)
    {
        consumerGeneration = current; pending = 0; primed = false;
        lastHostRate = 0;
        for (auto& i : interpolators) i.reset();
    }
    const double sourceRate = rate.load();
    if (std::abs(lastHostRate - hostRate) > 0.01)
    {
        lastHostRate = hostRate; filterState = {}; filtering = sourceRate > hostRate;
        if (filtering)
        {
            constexpr double q[] { 0.5097955791, 0.6013448869, 0.8999762231, 2.5629154477 };
            const double omega = juce::MathConstants<double>::twoPi * (hostRate * 0.45) / sourceRate;
            const double cosine = std::cos(omega), sine = std::sin(omega);
            for (std::size_t i = 0; i < 4; ++i)
            {
                const double alpha = sine / (2 * q[i]), a0 = 1 + alpha;
                filterCoefficients[i] = { (1 - cosine) / (2 * a0), (1 - cosine) / a0,
                    (1 - cosine) / (2 * a0), -2 * cosine / a0, (1 - alpha) / a0 };
            }
        }
    }
    const auto target = static_cast<std::size_t>(sourceRate * 0.04);
    if (!primed)
    {
        if (queue.available() < target) return;
        primed = true;
    }
    // Slowly correct independent device/host clock drift around a 40 ms reservoir.
    // Interpolator history is preserved across blocks. Bound correction to +/- 0.1%.
    const double fill = static_cast<double>(queue.available() + static_cast<std::size_t>(pending));
    const double correction = juce::jlimit(-0.001, 0.001, (fill - static_cast<double>(target)) / (sourceRate * 20.0));
    const double ratio = sourceRate / hostRate * (1.0 + correction);
    for (int offset = 0; offset < buffer.getNumSamples(); offset += 64)
    {
        const int count = juce::jmin(64, buffer.getNumSamples() - offset);
        const int needed = static_cast<int>(std::ceil(ratio * count)) + 2;
        if (needed > input.getNumSamples()) return; // validated rates keep this bounded
        const int previous = pending;
        if (pending < needed)
        {
            float* dst[] { input.getWritePointer(0, pending), input.getWritePointer(1, pending) };
            pending += static_cast<int>(queue.pop(dst, static_cast<std::size_t>(needed - pending)));
        }
        if (pending < needed)
        {
            // Windows sends no packets when an endpoint is idle; preserve wall-clock silence.
            for (int c = 0; c < 2; ++c) input.clear(c, pending, needed - pending);
            pending = needed;
        }
        if (filtering)
            for (int c = 0; c < 2; ++c)
                for (int i = previous; i < pending; ++i)
                {
                    double value = input.getSample(c, i);
                    for (std::size_t stage = 0; stage < 4; ++stage)
                    {
                        const auto& a = filterCoefficients[stage]; auto& z = filterState[static_cast<std::size_t>(c)][stage];
                        const double out = a[0] * value + z[0];
                        z[0] = a[1] * value - a[3] * out + z[1]; z[1] = a[2] * value - a[4] * out; value = out;
                    }
                    input.setSample(c, i, static_cast<float>(value));
                }
        const int used = interpolators[0].process(ratio, input.getReadPointer(0), buffer.getWritePointer(0, offset), count);
        interpolators[1].process(ratio, input.getReadPointer(1), buffer.getWritePointer(1, offset), count);
        pending -= used;
        for (int c = 0; c < 2; ++c)
            std::memmove(input.getWritePointer(c), input.getReadPointer(c, used), static_cast<std::size_t>(pending) * sizeof(float));
    }
}

#if LOOPCAPTURE_TESTING
void Capture::testBegin(double sampleRate)
{
    stop();
    while (!consumerGate.producerIsQuiescent()) juce::Thread::sleep(1);
    queue.resetWhileInactive(); rate.store(sampleRate); generation.fetch_add(1);
    setMessage("Synthetic endpoint (test executable only)", false);
    consumerGate.state.store(capture::State::recording);
}
#endif

#if JUCE_WINDOWS
namespace
{
// Local constants avoid SDK-specific external GUID libraries with MinGW/MSVC.
constexpr PROPERTYKEY friendlyNameKey { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
constexpr GUID pcmFormat { 1, 0, 0x10, { 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 } };
constexpr GUID floatFormat { 3, 0, 0x10, { 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 } };
template <class T> struct ComPtr
{
    T* ptr = nullptr;
    ~ComPtr() { if (ptr) ptr->Release(); }
    T* operator->() const { return ptr; }
    T** out() { return &ptr; }
};
struct Apartment
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
juce::String nameOf(IMMDevice* device)
{
    ComPtr<IPropertyStore> properties; PROPVARIANT value; PropVariantInit(&value);
    juce::String name("Salida de Windows");
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.out()))
        && SUCCEEDED(properties->GetValue(friendlyNameKey, &value)) && value.vt == VT_LPWSTR)
        name = juce::String(value.pwszVal);
    PropVariantClear(&value); return name;
}
}
#endif
std::vector<Capture::Device> Capture::devices(juce::String& error)
{
    std::vector<Device> result; error.clear();
#if JUCE_WINDOWS
    Apartment apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
    { error = "No se pudo inicializar Windows Audio."; return result; }
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> collection;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.out()));
    if (SUCCEEDED(hr)) hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.out());
    UINT count = 0;
    if (SUCCEEDED(hr)) hr = collection->GetCount(&count);
    if (FAILED(hr)) { error = "No se pudieron listar las salidas de Windows."; return result; }
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device; LPWSTR id = nullptr;
        if (SUCCEEDED(collection->Item(i, device.out())) && SUCCEEDED(device->GetId(&id)))
        { result.push_back({ juce::String(id), nameOf(device.ptr) }); CoTaskMemFree(id); }
    }
#else
    error = "En macOS elige un cable virtual en la entrada del DAW.";
#endif
    return result;
}
void Capture::run()
{
    consumerGate.state.store(capture::State::idle);
    while (!consumerGate.producerIsQuiescent()) { if (threadShouldExit()) return; wait(1); }
    queue.resetWhileInactive();
    captureDevice();
    consumerGate.state.store(capture::State::idle);
}
void Capture::captureDevice()
{
#if JUCE_WINDOWS
    Apartment apartment;
    if (FAILED(apartment.result)) { setMessage("Windows Audio no disponible.", true); return; }
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client; ComPtr<IAudioCaptureClient> capture;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.out()));
    if (SUCCEEDED(hr)) hr = deviceId.isEmpty() ? enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.out())
                                             : enumerator->GetDevice(deviceId.toWideCharPointer(), device.out());
    if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.out()));
    WAVEFORMATEX* raw = nullptr;
    if (SUCCEEDED(hr)) hr = client->GetMixFormat(&raw);
    const std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(raw, &CoTaskMemFree);
    Format decoder;
    if (SUCCEEDED(hr))
    {
        decoder = { raw->nChannels, raw->wBitsPerSample, raw->nBlockAlign, raw->wFormatTag == WAVE_FORMAT_IEEE_FLOAT };
        bool recognised = raw->wFormatTag == WAVE_FORMAT_PCM || raw->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (raw->wFormatTag == WAVE_FORMAT_EXTENSIBLE && raw->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            const auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(raw);
            decoder.floatingPoint = IsEqualGUID(ext->SubFormat, floatFormat);
            recognised = decoder.floatingPoint || IsEqualGUID(ext->SubFormat, pcmFormat);
            // Multichannel mix: capture the front stereo pair, never reinterpret an unknown layout.
            if (raw->nChannels > 2 && (ext->dwChannelMask & 3) != 3) recognised = false;
        }
        if (!recognised || !decoder.valid() || raw->nSamplesPerSec < 8000 || raw->nSamplesPerSec > 384000)
        { setMessage("Formato de salida no compatible (PCM/float, frontal L/R).", true); return; }
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                1000000, 0, raw, nullptr);
    }
    if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.out()));
    if (SUCCEEDED(hr)) hr = client->Start();
    if (FAILED(hr))
    {
        setMessage("No se pudo abrir la salida (0x" + juce::String::toHexString(static_cast<int>(hr))
                   + "). Comprueba el dispositivo y su modo compartido.", true); return;
    }
    rate.store(raw->nSamplesPerSec); generation.fetch_add(1);
    setMessage("Capturando: " + nameOf(device.ptr), false);
    if (!threadShouldExit()) consumerGate.state.store(capture::State::recording);
    juce::AudioBuffer<float> packet(2, 8192); bool firstPacket = true;
    while (!threadShouldExit())
    {
        UINT next = 0; hr = capture->GetNextPacketSize(&next);
        while (SUCCEEDED(hr) && next > 0 && !threadShouldExit())
        {
            BYTE* data = nullptr; UINT frames = 0; DWORD flags = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;
            if (hr == AUDCLNT_S_BUFFER_EMPTY) { hr = S_OK; break; }
            const bool discontinuity = !firstPacket && (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
            firstPacket = false; bool accepted = !discontinuity;
            for (UINT offset = 0; accepted && offset < frames; offset += 8192)
            {
                const int n = static_cast<int>(juce::jmin(UINT(8192), frames - offset));
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                accepted = decode(silent ? nullptr : data + offset * raw->nBlockAlign, n, decoder,
                                  packet.getWritePointer(0), packet.getWritePointer(1), silent)
                        && queue.push(packet.getArrayOfReadPointers(), static_cast<std::size_t>(n));
            }
            const HRESULT released = capture->ReleaseBuffer(frames);
            if (!accepted)
            {
                setMessage(discontinuity ? "Captura interrumpida por Windows. Reconecta para otra toma."
                                         : "El DAW no consume audio a tiempo. Captura detenida; reconecta.", true);
                consumerGate.state.store(capture::State::idle); client->Stop(); return;
            }
            hr = FAILED(released) ? released : capture->GetNextPacketSize(&next);
        }
        if (FAILED(hr)) { setMessage("El dispositivo se desconecto o fallo. Reconecta para continuar.", true); break; }
        wait(2); // WASAPI polling occurs only on this dedicated worker, never the host audio thread.
    }
    client->Stop();
#endif
}
}
