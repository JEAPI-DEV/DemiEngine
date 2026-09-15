#define BGFX_PROFILER_SCOPE(name, color)
enum Result { VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR,
              VK_ERROR_SURFACE_LOST_KHR };
struct Probe {
  bool m_needToRecreateSurface = false;
  bool m_needToRecreateSwapchain = false;
  bool acquire(Result result) {
    switch (result) {
      case VK_SUCCESS: break;
      case VK_ERROR_SURFACE_LOST_KHR:
        m_needToRecreateSurface = true;
        m_needToRecreateSwapchain = true;
        return false;
			case VK_ERROR_OUT_OF_DATE_KHR:
			case VK_SUBOPTIMAL_KHR:
        m_needToRecreateSwapchain = true;
        return false;
    }
    return true;
  }
  void present(Result result) {
    switch (result) {
      case VK_SUCCESS: break;
      case VK_ERROR_SURFACE_LOST_KHR:
        m_needToRecreateSurface = true;
        m_needToRecreateSwapchain = true;
        break;
			case VK_ERROR_OUT_OF_DATE_KHR:
			case VK_SUBOPTIMAL_KHR:
        m_needToRecreateSwapchain = true;
        break;
    }
  }
};
int main() {
  for (Result result : {VK_SUCCESS, VK_SUBOPTIMAL_KHR,
                        VK_ERROR_OUT_OF_DATE_KHR, VK_ERROR_SURFACE_LOST_KHR}) {
    bool usable = result == VK_SUCCESS || (BX_PLATFORM_ANDROID && result == VK_SUBOPTIMAL_KHR);
    Probe acquired;
    if (acquired.acquire(result) != usable ||
        acquired.m_needToRecreateSwapchain == usable ||
        acquired.m_needToRecreateSurface != (result == VK_ERROR_SURFACE_LOST_KHR)) return 1;
    Probe presented;
    presented.present(result);
    if (presented.m_needToRecreateSwapchain == usable ||
        presented.m_needToRecreateSurface != (result == VK_ERROR_SURFACE_LOST_KHR)) return 2;
  }
}
