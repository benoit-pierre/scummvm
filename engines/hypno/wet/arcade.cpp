#include "hypno/grammar.h"
#include "hypno/hypno.h"

#include "common/events.h"
#include "graphics/cursorman.h"

namespace Hypno {

bool WetEngine::clickedSecondaryShoot(const Common::Point &mousePos) {
	return clickedPrimaryShoot(mousePos);
}

void WetEngine::drawShoot(const Common::Point &mousePos) {
	uint32 c = _pixelFormat.RGBToColor(252, 252, 0);
	_compositeSurface->drawLine(0, _screenH, mousePos.x, mousePos.y, c);
	_compositeSurface->drawLine(0, _screenH, mousePos.x - 1, mousePos.y, c);
	_compositeSurface->drawLine(0, _screenH, mousePos.x - 2, mousePos.y, c);

	_compositeSurface->drawLine(_screenW, _screenH, mousePos.x, mousePos.y, c);
	_compositeSurface->drawLine(_screenW, _screenH, mousePos.x - 1, mousePos.y, c);
	_compositeSurface->drawLine(_screenW, _screenH, mousePos.x - 2, mousePos.y, c);
	playSound(_soundPath + _shootSound, 1);
}

void WetEngine::drawPlayer() {

	if (_playerFrameIdx < _playerFrameSep) {
		// TARGET ACQUIRED frame
		uint32 c = _pixelFormat.RGBToColor(32, 208, 32);
		_compositeSurface->drawLine(113, 1, 119, 1, c);
		_compositeSurface->drawLine(200, 1, 206, 1, c);

		_compositeSurface->drawLine(113, 1, 113, 9, c);
		_compositeSurface->drawLine(206, 1, 206, 9, c);

		_compositeSurface->drawLine(113, 9, 119, 9, c);
		_compositeSurface->drawLine(200, 9, 206, 9, c);

		c = _pixelFormat.RGBToColor(255, 0, 0);
		Common::Point mousePos = g_system->getEventManager()->getMousePos();
		int i = detectTarget(mousePos);
		if (i > 0)
			_font->drawString(_compositeSurface, "TARGET  ACQUIRED", 120, 1, 80, c);

		_playerFrameIdx++;
		_playerFrameIdx = _playerFrameIdx % _playerFrameSep;
	} else {
		_playerFrameIdx++;
		if (_playerFrameIdx >= _playerFrames.size())
			_playerFrameIdx = 0;
	}

	drawImage(*_playerFrames[_playerFrameIdx], 0, 200 - _playerFrames[_playerFrameIdx]->h + 1, true);
}

static const int uiPos[2][3][2] = {
	{{70, 160}, {180, 160}, {220, 185}}, // c31
	{{60, 167}, {190, 167}, {135, 187}}, // c52
};

void WetEngine::drawHealth() {
	uint32 c = _pixelFormat.RGBToColor(252, 252, 0);
	int p = (100 * _health) / _maxHealth;
	int s = _score;
	if (_playerFrameIdx < _playerFrameSep) {
		uint32 id = _levelId;
		_font->drawString(_compositeSurface, Common::String::format("ENERGY   %d%%", p), uiPos[id][0][0], uiPos[id][0][1], 65, c);
		_font->drawString(_compositeSurface, Common::String::format("SCORE    %04d", s), uiPos[id][1][0], uiPos[id][1][1], 72, c);
		// Objectives are always in the zero in the demo
		_font->drawString(_compositeSurface, Common::String::format("M.O.     0/0"), uiPos[id][2][0], uiPos[id][2][1], 60, c);
	}
}

} // End of namespace Hypno