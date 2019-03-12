//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THE SOFTWARE IS PROVIDED �AS IS�, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH 
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//*********************************************************

#include "stdafx.h"
#include "WinComp.h"

WinComp* WinComp::s_instance;

WinComp::WinComp() 
{

}

WinComp* WinComp::GetInstance()
{
	if (s_instance == NULL)
		s_instance = new WinComp();
	return s_instance;
}

WinComp::~WinComp()
{
	delete s_instance;
}

DispatcherQueueController WinComp::EnsureDispatcherQueue()
{
	namespace abi = ABI::Windows::System;

	DispatcherQueueOptions options
	{
		sizeof(DispatcherQueueOptions),
		DQTYPE_THREAD_CURRENT,
		DQTAT_COM_ASTA
	};

	DispatcherQueueController controller{ nullptr };
	check_hresult(CreateDispatcherQueueController(options, reinterpret_cast<abi::IDispatcherQueueController**>(put_abi(controller))));

	return controller;

}

DesktopWindowTarget WinComp::CreateDesktopWindowTarget(Compositor const& compositor, HWND window)
{
	namespace abi = ABI::Windows::UI::Composition::Desktop;

	auto interop = compositor.as<abi::ICompositorDesktopInterop>();
	DesktopWindowTarget target{ nullptr };
	check_hresult(interop->CreateDesktopWindowTarget(window, true, reinterpret_cast<abi::IDesktopWindowTarget**>(put_abi(target))));
	return target;
}

void WinComp::Initialize(HWND hwnd)
{
	namespace abi = ABI::Windows::UI::Composition;

	m_window = hwnd;
	Compositor compositor;
	m_compositor = compositor;
	DirectXTileRenderer* dxRenderer = new DirectXTileRenderer();
	dxRenderer->Initialize(m_compositor, TileDrawingManager::TILESIZE);
	m_TileDrawingManager.setRenderer(dxRenderer);

}

void WinComp::TryRedirectForManipulation(PointerPoint pp)
{
	m_interactionSource.TryRedirectForManipulation(pp);
}

void WinComp::TryUpdatePositionBy(float3 const& amount)
{
	m_tracker.TryUpdatePositionBy(amount);
}

void WinComp::PrepareVisuals()
{
	m_target = CreateDesktopWindowTarget(m_compositor, m_window);
	
	auto root = m_compositor.CreateSpriteVisual();
	//Create a background with Gray color brush.
	root.Brush(m_compositor.CreateColorBrush({ 0xFF, 0xFE, 0xFE , 0xFE }));

	root.Size(getWindowSize());
	m_target.Root(root);
	
	auto visuals = root.Children();
	AddD2DVisual(visuals, 0.0f, 0.0f);
}

void WinComp::AddVisual(VisualCollection const& visuals, float x, float y)
{
	auto visual = m_compositor.CreateSpriteVisual();
	static Color colors[] =
	{
		{ 0xDC, 0x5B, 0x9B, 0xD5 },
		{ 0xDC, 0xFF, 0xC0, 0x00 },
		{ 0xDC, 0xED, 0x7D, 0x31 },
		{ 0xDC, 0x70, 0xAD, 0x47 },
	};
	static unsigned last = 0;
	unsigned const next = ++last % _countof(colors);
	visual.Brush(m_compositor.CreateColorBrush(colors[next]));
	visual.Size(
		{
			100.0f,
			100.0f
		});

	visual.Offset(
		{
			x,
			y,
			0.0f,
		});
	visuals.InsertAtTop(visual);
}

void WinComp::AddD2DVisual(VisualCollection const& visuals, float x, float y)
{
	auto compositor = visuals.Compositor();
	m_contentVisual = compositor.CreateSpriteVisual();
	m_contentVisual.Brush(m_TileDrawingManager.getRenderer()->getSurfaceBrush());

	m_contentVisual.Size(getWindowSize());
	m_contentVisual.Offset({ x, y, 0.0f, });

	visuals.InsertAtTop(m_contentVisual);
}

void WinComp::UpdateViewPort(boolean changeContentVisual)
{
	//return if the m_window hasn't been set.
	if(m_window!=nullptr){
		RECT windowRect;
		::GetWindowRect(m_window, &windowRect);
		Size windowSize ;
		windowSize.Height = (windowRect.bottom - windowRect.top)/m_lastTrackerScale;
		windowSize.Width = (windowRect.right - windowRect.left)/m_lastTrackerScale;

		
		if(changeContentVisual){
			m_contentVisual.Size(windowSize);
		}
		m_TileDrawingManager.UpdateViewportSize(windowSize);
		m_TileDrawingManager.UpdateVisibleRegionByRange(m_lastTrackerPosition / m_lastTrackerScale);
	}
}

Size WinComp::getWindowSize()
{
	RECT windowRect;
	::GetWindowRect(m_window, &windowRect);

	return Size({ 0.0f + windowRect.right - windowRect.left, 0.0f + windowRect.bottom - windowRect.top });

}

void WinComp::StartAnimation(CompositionSurfaceBrush brush)
{
	m_animatingPropset = m_compositor.CreatePropertySet();
	m_animatingPropset.InsertScalar(L"xcoord", 1.0f);
	m_animatingPropset.StartAnimation(L"xcoord", m_moveSurfaceExpressionAnimation);

	m_animatingPropset.InsertScalar(L"ycoord", 1.0f);
	m_animatingPropset.StartAnimation(L"ycoord", m_moveSurfaceUpDownExpressionAnimation);

	m_animatingPropset.InsertScalar(L"scale", 1.0f);
	m_animatingPropset.StartAnimation(L"scale", m_scaleSurfaceUpDownExpressionAnimation);

	m_animateMatrix = m_compositor.CreateExpressionAnimation(L"Matrix3x2(props.scale, 0.0, 0.0, props.scale, props.xcoord, props.ycoord)");
	m_animateMatrix.SetReferenceParameter(L"props", m_animatingPropset);

	brush.StartAnimation(L"TransformMatrix", m_animateMatrix);
}

void WinComp::ConfigureInteraction()
{
	m_interactionSource = VisualInteractionSource::Create(m_contentVisual);
	m_interactionSource.PositionXSourceMode(InteractionSourceMode::EnabledWithInertia);
	m_interactionSource.PositionYSourceMode(InteractionSourceMode::EnabledWithInertia);
	m_interactionSource.ScaleSourceMode(InteractionSourceMode::EnabledWithInertia);
	m_interactionSource.ManipulationRedirectionMode(VisualInteractionSourceRedirectionMode::CapableTouchpadAndPointerWheel);

	//IInteractionTrackerOwner itOwner = (auto &&) { make<WinComp>() };
	m_tracker = InteractionTracker::CreateWithOwner(m_compositor, *this);
	m_tracker.InteractionSources().Add(m_interactionSource);
	
	m_moveSurfaceExpressionAnimation = m_compositor.CreateExpressionAnimation(L"-tracker.Position.X");
	m_moveSurfaceExpressionAnimation.SetReferenceParameter(L"tracker", m_tracker);
	
	m_moveSurfaceUpDownExpressionAnimation = m_compositor.CreateExpressionAnimation(L"-tracker.Position.Y");
	m_moveSurfaceUpDownExpressionAnimation.SetReferenceParameter(L"tracker", m_tracker);
	
	m_scaleSurfaceUpDownExpressionAnimation = m_compositor.CreateExpressionAnimation(L"tracker.Scale");
	m_scaleSurfaceUpDownExpressionAnimation.SetReferenceParameter(L"tracker", m_tracker);
	
	m_tracker.MinPosition(float3(0, 0, 0));
	m_tracker.MaxPosition(float3(TileDrawingManager::TILESIZE * 10000, TileDrawingManager::TILESIZE * 10000, 0));

	//TODO: VirtualSurface.BeginDraw fails when we try to draw to a very large surface inside one BeginDraw call. 
	//Need more complex logic there to handle it well.
	m_tracker.MinScale(0.2f);
	m_tracker.MaxScale(3.0f);
	
	StartAnimation(m_TileDrawingManager.getRenderer()->getSurfaceBrush());
}

// interactionTrackerowner

void WinComp::CustomAnimationStateEntered(InteractionTracker sender, InteractionTrackerCustomAnimationStateEnteredArgs args)
{
}

void WinComp::IdleStateEntered(InteractionTracker sender, InteractionTrackerIdleStateEnteredArgs args)
{
	if (m_zooming)
	{
		//dont update the content visual, because the window size hasnt changed.
		UpdateViewPort( false);
	}

	m_zooming = false;
}

void WinComp::InertiaStateEntered(InteractionTracker sender, InteractionTrackerInertiaStateEnteredArgs args)
{
}

void WinComp::InteractingStateEntered(InteractionTracker sender, InteractionTrackerInteractingStateEnteredArgs args)
{

}

void WinComp::RequestIgnored(InteractionTracker sender, InteractionTrackerRequestIgnoredArgs args)
{
}

void WinComp::ValuesChanged(InteractionTracker sender, InteractionTrackerValuesChangedArgs args)
{
	try
	{
		
		if (m_lastTrackerScale == args.Scale())
		{
			m_TileDrawingManager.UpdateVisibleRegionByRange(sender.Position()/m_lastTrackerScale);
		}
		else
		{
			// Don't run tilemanager during a zoom
			m_zooming = true;
		}

		m_lastTrackerScale = args.Scale();
		m_lastTrackerPosition = sender.Position();
	}
	catch (...)
	{
	//	Debug.WriteLine(ex.Message);
	}
}








