#include "UEVideoRecorderPrivatePCH.h"
#include "VideoRecordGameViewportClient.h"

#if defined _MSC_FULL_VER && _MSC_FULL_VER < 190023506
#error old compiler version
#endif

#pragma region Traits
template<typename Signature>
struct FunctionTraits;

template<typename Ret, typename ...Args>
struct FunctionTraits<Ret(Args...)>
{
	typedef Ret ret;
	static constexpr auto arity = sizeof...(Args);
	template<size_t i>
	using arg = typename std::tuple_element<i, std::tuple<Args...>>::type;
};

#define DECL_MEMBER_FUNCTION_TRAITS(cv)															\
	template<class Class, typename Ret, typename ...Args>										\
	struct FunctionTraits<Ret (Class::*)(Args...) cv> : FunctionTraits<Ret (Class &, Args...)>	\
	{};

DECL_MEMBER_FUNCTION_TRAITS()
DECL_MEMBER_FUNCTION_TRAITS(const)
DECL_MEMBER_FUNCTION_TRAITS(volatile)
DECL_MEMBER_FUNCTION_TRAITS(const volatile)

#undef DECL_MEMBER_FUNCTION_TRAITS

#define DECL_HAS_MEMBER(Member)					\
	template<class Class, typename = void>		\
	struct Has_##Member : std::false_type {};	\
												\
	template<class Class>						\
	struct Has_##Member<Class, decltype(std::declval<Class>().Member, void()) : std::true_type {};

#define HAS_MEMBER(Class, Member) Has_##Member<Class>::value;

#define DECL_HAS_FUNCTION(Function)												\
	template<class Class>														\
	class HasFunction_##Function												\
	{																			\
		struct Ambiguator														\
		{																		\
			void Function() {}													\
		};																		\
																				\
		struct Derived : Class, Ambiguator {};									\
																				\
		template<void (Ambiguator::*)()>										\
		struct Absorber;														\
																				\
		template<class CheckedClass>											\
		static std::false_type Check(Absorber<&CheckedClass::Function> *);		\
																				\
		template<class CheckedClass>											\
		static std::true_type Check(...);										\
																				\
	public:																		\
		static constexpr bool value = decltype(Check<Derived>(nullptr))::value;	\
	};

#define HAS_FUNCTION(Class, Function) HasFunction_##Function<Class>::value
#pragma endregion

#pragma region Logger
	DECL_HAS_FUNCTION(Logf)
	DECL_HAS_FUNCTION(Logf_Internal)

	DEFINE_LOG_CATEGORY(VideoRecorder)

#if 0
namespace
{
	template<typename Signature>
	struct LogSignature;

	template<typename Ret, typename ...Args>
	struct LogSignature<Ret(Args)>
	{
		typedef Ret(*Log)(Args...);
	}

	template<typename ...Args, typename Ret, Ret(*Log)(Args...)>
	inline void Log(ELogVerbosity::Type verbosity, const wchar_t msgStr[])
	{
		Log(__FILE__, __LINE__, VideoRecorder.GetCategoryName(), verbosity, msgStr);
	}
}
#elif 0
#define LOG_ARGS __FILE__, __LINE__, VideoRecorder.GetCategoryName(), verbosity, msgStr.c_str()
#endif

template<bool = true, typename = void>
struct Logger
{
	static void Log(...) {}
};

// workaround for VS 2015 bug
#if defined _MSC_FULL_VER && _MSC_FULL_VER == 190023506
#define INST_TEMPLATE(Function) template class HasFunction_##Function<FMsg>;
#else
#define INST_TEMPLATE(Function)
#endif

#define DECL_LOGGER(Logf, condition)																	\
	INST_TEMPLATE(Logf)																					\
	template<bool True>																					\
	struct Logger<True, typename std::enable_if<True && HAS_FUNCTION(FMsg, Logf) && condition>::type>	\
	{																									\
		template<typename ...Args>																		\
		static decltype(auto) Log(Args &&...args)														\
		{																								\
			return FMsg::Logf(std::forward<Args>(args)...);												\
		}																								\
	};

DECL_LOGGER(Logf, true)
DECL_LOGGER(Logf_Internal, !HAS_FUNCTION(FMsg, Logf))
#pragma endregion

#pragma region LogRedirect
std::streamsize LogRedirect::LogSink::write(const wchar_t msg[], std::streamsize count)
{
	static const wchar_t fmt[] = L"%.*ls";
	//UE_LOG(VideoRecorder, verbosity, msgStr.c_str());
#if NO_LOGGING
	if (verbosity == ELogVerbosity::Fatal)
	{ 
		LowLevelFatalErrorHandler(__FILE__, __LINE__, fmt, (int)count, msg);
		_DebugBreakAndPromptForRemote();
		FDebug::AssertFailed("", __FILE__, __LINE__, fmt, (int)count, msg);
		UE_LOG_EXPAND_IS_FATAL(Fatal, CA_ASSUME(false);, PREPROCESSOR_NOTHING);
	}
#else
#if 0
	Log<FMsg::Logf>(verbosity, msgStr.c_str());
#elif 0
	FMsg::Logf(__FILE__, __LINE__, VideoRecorder.GetCategoryName(), verbosity, msg);
#else
	Logger<>::Log(__FILE__, __LINE__, VideoRecorder.GetCategoryName(), verbosity, fmt, (int)count, msg);
#endif
#endif
	return count;
}

LogRedirect::LogRedirect(std::wostream &src, ELogVerbosity::Type verbosity) :
	src(src),
	redirectStream(LogSink(verbosity)),
	oldbuff(src.rdbuf(&redirectStream))
{}

LogRedirect::~LogRedirect()
{
	src.rdbuf(oldbuff);
}
#pragma endregion

#if !LEGACY
typedef CVideoRecorder::CFrame::FrameData::Format FrameFormat;

#ifdef ENABLE_ASYNC
using WRL::ComPtr;

namespace
{
	inline void AssertHR(HRESULT hr)
	{
		assert(SUCCEEDED(hr));
	}

	inline void CheckHR(HRESULT hr)
	{
		if (FAILED(hr))
			throw hr;
	}

	inline bool Unused(IUnknown *object)
	{
		object->AddRef();
		return object->Release() == 1;
	}

	inline FrameFormat GetFrameFormat(DXGI_FORMAT DXFormat)
	{
		switch (DXFormat)
		{
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			return FrameFormat::B8G8R8A8;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return FrameFormat::R8G8B8A8;
		case DXGI_FORMAT_R10G10B10A2_TYPELESS:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return FrameFormat::R10G10B10A2;
		default:
			throw std::logic_error("unsupported texture format: " + std::to_string(DXFormat));
		}
	}

	inline ID3D11Texture2D *GetRendertargetTexture(FViewport *viewport)
	{
		const auto texture = reinterpret_cast<ID3D11Texture2D *>(viewport->GetViewportRHI()
			? viewport->GetViewportRHI()->GetNativeBackBufferTexture()
			: viewport->GetRenderTargetTexture()->GetNativeResource());
		assert(texture);
		return texture;
	}
}

#pragma region texture pool
// probably will not be inlined if pass it to algos, functor/lambda needed in order to it happens
inline bool UVideoRecordGameViewportClient::CTexturePool::Unused(decltype(pool)::const_reference texture)
{
	return ::Unused(texture.texture.Get());
}

#define SEARCH_SMALLEST 1
ComPtr<ID3D11Texture2D> UVideoRecordGameViewportClient::CTexturePool::GetTexture(ID3D11Device *device, DXGI_FORMAT format, unsigned int width, unsigned int height)
{
	const auto IsAcceptable = [format](decltype(pool)::const_reference texture)
	{
		return Unused(texture) && [&texture, format]
		{
			D3D11_TEXTURE2D_DESC desc;
			texture.texture->GetDesc(&desc);
			return desc.Format == format;
		}();
	};
#if SEARCH_SMALLEST
	const auto cached = std::min_element(pool.begin(), pool.end(), [format](decltype(pool)::const_reference left, decltype(pool)::const_reference right)
	{
		const bool leftUnused = Unused(left), rightUnused = Unused(right);
		if (leftUnused != rightUnused)
			return leftUnused;

		D3D11_TEXTURE2D_DESC leftDesc, rightDesc;
		left.texture->GetDesc(&leftDesc);
		right.texture->GetDesc(&rightDesc);
		if (leftDesc.Format != rightDesc.Format)
			return leftDesc.Format == format;
		return leftDesc.Width * leftDesc.Height < rightDesc.Width * rightDesc.Height;
	});
#else
	const auto cached = std::find_if(pool.begin(), pool.end(), IsAcceptable);
#endif

#if SEARCH_SMALLEST
	if (cached != pool.end() && IsAcceptable(*cached))
#else
	if (cached != pool.end())
#endif
	{
		cached->idleTime = 0;
		return cached->texture;
	}

	ComPtr<ID3D11Texture2D> texture;
	CheckHR(device->CreateTexture2D(&CD3D11_TEXTURE2D_DESC(format, width, height, 1, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ), NULL, &texture));
	pool.emplace_front(std::move(texture));
	return pool.front().texture;
}
#undef SEARCH_SMALLEST

void UVideoRecordGameViewportClient::CTexturePool::NextFrame()
{
	pool.remove_if([](decltype(pool)::reference texture)
	{
		return Unused(texture) && ++texture.idleTime > maxIdle;
	});
}
#pragma endregion
#endif

#pragma region CFrame
#ifdef ENABLE_ASYNC
template<>
class UVideoRecordGameViewportClient::CFrame<true> final : public CVideoRecorder::CFrame
{
	ComPtr<ID3D11Texture2D> stagingTexture;
	FrameData frameData;

private:
	FrameData GetFrameData() const override { return frameData; }

private:
	ComPtr<ID3D11DeviceContext> GetContext() const;

public:
	CFrame(CFrame::Opaque opaque, CTexturePool &texturePool, ID3D11Device *device, DXGI_FORMAT format, unsigned int width, unsigned int height);
	~CFrame();

public:
	void operator =(ID3D11Texture2D *src);
	bool TrySubmit(UINT waitFlags);
};

ComPtr<ID3D11DeviceContext> UVideoRecordGameViewportClient::CFrame<true>::GetContext() const
{
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	stagingTexture->GetDevice(&device);
	device->GetImmediateContext(&context);
	return context;
}

UVideoRecordGameViewportClient::CFrame<true>::CFrame(CFrame::Opaque opaque, CTexturePool &texturePool, ID3D11Device *device, DXGI_FORMAT format, unsigned int width, unsigned int height) :
CVideoRecorder::CFrame(std::forward<CFrame::Opaque>(opaque)),
stagingTexture(texturePool.GetTexture(device, format, width, height)),
frameData()
{
	frameData.format = GetFrameFormat(format);
	frameData.width = width;
	frameData.height = height;
}

UVideoRecordGameViewportClient::CFrame<true>::~CFrame()
{
	if (frameData.pixels)
	{
		ENQUEUE_RENDER_COMMAND(UnmapCommand)([texture = stagingTexture](FRHICommandListImmediate &)
			{
				ComPtr<ID3D11Device> device;
				ComPtr<ID3D11DeviceContext> context;
				texture->GetDevice(&device);
				device->GetImmediateContext(&context);
				context->Unmap(texture.Get(), 0);
			});
	}
}

void UVideoRecordGameViewportClient::CFrame<true>::operator =(ID3D11Texture2D *src)
{
	D3D11_TEXTURE2D_DESC desc;
	src->GetDesc(&desc);
	const CD3D11_BOX box(0, 0, 0, desc.Width, desc.Height, 1);
	GetContext()->CopySubresourceRegion(stagingTexture.Get(), 0, 0, 0, 0, src, 0, &box);
}

bool UVideoRecordGameViewportClient::CFrame<true>::TrySubmit(UINT waitFlags)
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	switch (const HRESULT hr = GetContext()->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, waitFlags, &mapped))
	{
	case S_OK:
		frameData.stride = mapped.RowPitch;
		frameData.pixels = mapped.pData;
		Ready();
		return true;
	case DXGI_ERROR_WAS_STILL_DRAWING:
		return false;
	default:
		throw hr;
	}
}
#endif
template<>
class UVideoRecordGameViewportClient::CFrame<false> final : public CVideoRecorder::CFrame
{
	TArray<FColor> frame;
	FIntPoint frameSize;

private:
	FrameData GetFrameData() const override;

public:
	CFrame(CFrame::Opaque opaque, FViewport *viewport);
};

UVideoRecordGameViewportClient::CFrame<false>::CFrame(CFrame::Opaque opaque, FViewport *viewport) :
CVideoRecorder::CFrame(std::forward<CFrame::Opaque>(opaque)),
frameSize(viewport->GetSizeXY())
{
	const auto ok = viewport->ReadPixels(frame, FReadSurfaceDataFlags(), FIntRect(0, 0, frameSize.X, frameSize.Y));
	assert(ok);
}

auto UVideoRecordGameViewportClient::CFrame<false>::GetFrameData() const -> FrameData
{
	return{ FrameFormat::B8G8R8A8, (unsigned int)frameSize.X, (unsigned int)frameSize.Y, frameSize.X * sizeof FColor, (const uint8_t *)frame.GetData() };
}
#pragma endregion
#endif

UVideoRecordGameViewportClient::UVideoRecordGameViewportClient()
try
{
#ifdef ENABLE_ASYNC
	if (!async)
		UE_LOG(VideoRecorder, Warning, TEXT("Async frame capture mode disabled. It can significantly impact rendering performance, especially for complex scenes. Async mode is currently supported only with D3D11."));
#endif
}
catch (...)
{
	UE_LOG(VideoRecorder, Fatal, TEXT("Fail to init game viewport client for video recorder."));
}

#ifdef ENABLE_ASYNC
bool UVideoRecordGameViewportClient::DetectAsyncMode()
{
	switch (GMaxRHIShaderPlatform)
	{
	case SP_PCD3D_SM5:
#if ENGINE_MINOR_VERSION <= 24
	case SP_PCD3D_SM4:
	case SP_PCD3D_ES2:
#endif
	case SP_PCD3D_ES3_1:
		return true;
	default:
		return false;
	}
}

UVideoRecordGameViewportClient::~UVideoRecordGameViewportClient()
{
	if (async)
	{
		ENQUEUE_RENDER_COMMAND(FlushVideoFramesCommand)([this](FRHICommandListImmediate &)
			{
				try
				{
					while (!frameQueue.empty())
					{
						frameQueue.front()->TrySubmit(0);
						frameQueue.pop_front();
					}
				}
				catch (HRESULT hr)
				{
					Error(hr);
				}
				catch (const std::exception &error)
				{
					Error(error);
				}
			});

		FlushRenderingCommands();
	}
}
#endif

void UVideoRecordGameViewportClient::Draw(FViewport *viewport, FCanvas *sceneCanvas)
{
	if (!captureGUI)
		Super::Draw(viewport, sceneCanvas);

#if LEGACY
	FIntPoint frameSize = Viewport->GetSizeXY();
	Draw(frameSize.X, frameSize.Y, [=](std::decay_t<FunctionTraits<decltype(&CVideoRecorder::Draw)>::arg<3>>::argument_type data)
	{
		const auto ok = Viewport->ReadPixelsPtr(reinterpret_cast<FColor *>(data), FReadSurfaceDataFlags(), FIntRect(0, 0, frameSize.X, frameSize.Y));
		assert(ok);
	});
#else
#ifdef GAME
#define GET_TEXTURE GetViewportRHI()->GetNativeBackBufferTexture()
#else
#define GET_TEXTURE GetRenderTargetTexture()->GetNativeResource()
#endif
#ifdef ENABLE_ASYNC
	if (async)
	{
		ENQUEUE_RENDER_COMMAND(ProcessVideoRecordingCommand)([this](FRHICommandListImmediate &)
			{
				try
				{
					// try to submit ready frames
					while (!frameQueue.empty())
					{
						if (!frameQueue.front()->TrySubmit(D3D11_MAP_FLAG_DO_NOT_WAIT))
							break;
						frameQueue.pop_front();
					}

					if (Viewport)
					{
						SampleFrame([this](CFrame<true>::Opaque opaque)
						{
							//ID3D11Texture2D *const rt = reinterpret_cast<ID3D11Texture2D *>(Viewport->GET_TEXTURE);
							ID3D11Texture2D *const rt = GetRendertargetTexture(Viewport);

							ComPtr<ID3D11Device> device;
							rt->GetDevice(&device);

							ComPtr<ID3D11DeviceContext> context;
							device->GetImmediateContext(&context);

							D3D11_TEXTURE2D_DESC desc;
							rt->GetDesc(&desc);

							frameQueue.push_back(
								std::make_shared<CFrame<true>>(std::forward<CFrame<true>::Opaque>(opaque),
								texturePool, device.Get(), desc.Format, desc.Width, desc.Height));
							*frameQueue.back() = rt;

							return frameQueue.back();
						});

						texturePool.NextFrame();
					}
				}
				catch (HRESULT hr)
				{
					Error(hr);
				}
				catch (const std::exception &error)
				{
					Error(error);
				}
			});
	}
	else
#endif
	SampleFrame([this](CFrame<false>::Opaque opaque)
	{
		auto frame = std::make_shared<CFrame<false>>(std::forward<CFrame<false>::Opaque>(opaque), Viewport);
		frame->Ready();
		return frame;
	});
#endif

	if (captureGUI)
		Super::Draw(viewport, sceneCanvas);
}

// 1 call site
inline void UVideoRecordGameViewportClient::StartRecordImpl(const CallTarget &target, unsigned int width, unsigned int height, Format format)
{
	if (!width)
		width = Viewport->GetSizeXY().X;
	if (!height)
		height = Viewport->GetSizeXY().Y;
	target(width, height, format);
}

void UVideoRecordGameViewportClient::StartRecordImpl(CallTarget &&target, unsigned int width, unsigned int height, ::VideoFormat videoFormat)
{
#ifdef ENABLE_ASYNC
	if (async)
	{
		ENQUEUE_RENDER_COMMAND(StartRecordCommand)([this, target = std::move(target), width, height, videoFormat](FRHICommandListImmediate &)
			{
				try
				{
					const Format format = [this, videoFormat]
					{
						switch (videoFormat)
						{
						case VideoFormat::AUTO:
						{
							//ID3D11Texture2D *const rt = reinterpret_cast<ID3D11Texture2D *>(Viewport->GET_TEXTURE);
							ID3D11Texture2D *const rt = GetRendertargetTexture(Viewport);
							D3D11_TEXTURE2D_DESC desc;
							rt->GetDesc(&desc);
							return GetFrameFormat(desc.Format) == FrameFormat::R10G10B10A2 ? Format::_10bit : Format::_8bit;
						}
						case VideoFormat::_8:
							return Format::_8bit;
						case VideoFormat::_10:
							return Format::_10bit;
						default:
							assert(false);
							__assume(false);
						}
					}();
					StartRecordImpl(target, width, height, format);
				}
				catch (const std::exception &error)
				{
					Error(error);
				}
			});
	}
	else
#endif
	StartRecordImpl(target, width, height, videoFormat == ::VideoFormat::_10 ? Format::_10bit : Format::_8bit);
}

typedef std::make_signed_t<std::underlying_type_t<::Preset>> IntermediateRawPresetType;

void UVideoRecordGameViewportClient::StartRecord(std::wstring filename, unsigned int width, unsigned int height, ::VideoFormat format, ::FPS fps, ::Codec codec, int64_t crf, ::Preset preset)
{
	using namespace std;
	using namespace placeholders;
	StartRecordImpl(bind(&CVideoRecorder::StartRecord, this, move(filename), _1, _2, _3, FPS(fps), Codec(codec), crf, Preset((IntermediateRawPresetType)preset)), width, height, format);
}

void UVideoRecordGameViewportClient::StartRecordNV(std::wstring filename, unsigned int width, unsigned int height, ::VideoFormat format, ::FPS fps, ::Codec codec, int64_t cq, ::PresetNV preset)
{
	using namespace std;
	using namespace placeholders;
	StartRecordImpl(bind(&CVideoRecorder::StartRecordNV, this, move(filename), _1, _2, _3, FPS(fps), Codec(codec), cq, PresetNV((IntermediateRawPresetType)preset)), width, height, format);
}

#ifdef ENABLE_ASYNC
void UVideoRecordGameViewportClient::StopRecord()
{
	if (async)
	{
		ENQUEUE_RENDER_COMMAND(StopRecordCommand)([this](FRHICommandListImmediate &)
			{
				CVideoRecorder::StopRecord();
			});
	}
	else
		CVideoRecorder::StopRecord();
}

void UVideoRecordGameViewportClient::Screenshot(std::wstring filename)
{
	if (async)
	{
		ENQUEUE_RENDER_COMMAND(ScreenshotCommand)([this, filename](FRHICommandListImmediate &)
			{
				CVideoRecorder::Screenshot(std::move(filename));
			});
	}
	else
		CVideoRecorder::Screenshot(std::move(filename));
}
#endif

#ifdef ENABLE_ASYNC
#define ERROR_MSG_PREFIX TEXT("An Error has occurred: ")
#define ERROR_MSG_POSTFIX TEXT(". Any remaining pending frames being canceled.")

namespace
{
	template<typename Char = char>
	inline typename std::enable_if<std::is_same<Char, TCHAR>::value, const Char *>::type Str_char_2_TCHAR(const char *str)
	{
		return str;
	}

	template<typename Char>
	class c_str
	{
		std::basic_string<Char> str;

	public:
		c_str(decltype(str) && str) noexcept : str(std::move(str)) {}
		operator const Char *() const noexcept{ return str.c_str(); }
	};

	template<typename Char = char>
	typename std::enable_if<!std::is_same<Char, TCHAR>::value, c_str<TCHAR>>::type Str_char_2_TCHAR(const char *str)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<TCHAR>> converter;
		return converter.from_bytes(str);
	}
}

void UVideoRecordGameViewportClient::Error()
{
	CVideoRecorder::StopRecord();
	std::for_each(frameQueue.cbegin(), frameQueue.cend(), std::mem_fn(&CFrame<true>::Cancel));
	frameQueue.clear();
}

void UVideoRecordGameViewportClient::Error(HRESULT hr)
{
	UE_LOG(VideoRecorder, Error, ERROR_MSG_PREFIX TEXT("hr=%d") ERROR_MSG_POSTFIX, hr);
	Error();
}

void UVideoRecordGameViewportClient::Error(const std::exception &error)
{
	UE_LOG(VideoRecorder, Error, ERROR_MSG_PREFIX TEXT("%s") ERROR_MSG_POSTFIX, (const TCHAR *)Str_char_2_TCHAR(error.what()));
	Error();
}
#endif