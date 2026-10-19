"""Author the small boot scene independently of landscape and character art."""

BACKGROUND_ID = 'asset://amber_pass/loading/background'

BACKGROUND = '''<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="720" viewBox="0 0 1280 720">
<defs>
 <linearGradient id="sky" x2="0" y2="1"><stop stop-color="#121d29"/><stop offset="1" stop-color="#806b48"/></linearGradient>
 <linearGradient id="veil" x2="0" y2="1"><stop stop-color="#071018" stop-opacity="0"/><stop offset="1" stop-color="#071018"/></linearGradient>
</defs>
<path fill="url(#sky)" d="M0 0H1280V720H0Z"/>
<circle cx="1000" cy="180" r="70" fill="#dcc69c" opacity=".23"/>
<path fill="#34404a" d="M0 320L80 240 140 280 250 145 290 195 360 130 450 285 540 240 620 335 720 300 830 180 890 245 975 130 1030 210 1130 160 1280 310V720H0Z"/>
<path fill="#26343b" d="M0 400L100 340 210 370 300 260 400 315 510 380 650 430 790 310 875 360 970 300 1100 350 1280 290V720H0Z"/>
<path fill="#192930" d="M0 480Q210 350 430 460T850 430T1280 415V720H0Z"/>
<path fill="#766247" opacity=".55" d="M600 720Q690 595 660 515Q625 460 730 430Q635 462 690 513Q740 580 850 720Z"/>
<g fill="#101d24">
 <path d="M280 430V218H300V207H318V218H335V430Z"/>
 <path d="M420 445V268H440V257H460V268H475V445Z"/>
 <path d="M325 430V322Q380 245 435 322V430H413V330Q380 290 347 330V430Z"/>
</g>
<g stroke="#a88c55" stroke-width="2" opacity=".4">
 <path d="M300 242H330M300 264H330M300 286H330M300 308H330M300 330H330M440 292H468M440 314H468"/>
</g>
<path fill="url(#veil)" d="M0 300H1280V720H0Z"/>
</svg>'''


def label(identifier, text, position, size, font_size, color):
    return {
        'id': identifier,
        'type': 'label',
        'text': text,
        'at': position,
        'size': size,
        'font_size': font_size,
        'text_color': color,
    }


def write_documents(project, write_json, manifest):
    artwork = project / 'assets/loading/background.svg'
    artwork.parent.mkdir(parents=True, exist_ok=True)
    artwork.write_text(BACKGROUND)
    manifest(artwork, 'loading/background', 'SvgTexture2D', 'svg', {
        'filter': 'bilinear',
        'mipmaps': False,
        'wrap': 'clamp',
    })
    write_json(project / 'hud/loading.hud.json', {
        'format_version': 1,
        'canvas_size': [1280, 720],
        'children': [
            {
                'id': 'loading_background',
                'type': 'image',
                'texture': BACKGROUND_ID,
                'dock': 'fill',
                'respect_safe_area': False,
            },
            label('loading_title', 'AMBER PASS', [72, 60], [600, 60], 38, '#EBD7AEFF'),
            label('loading_subtitle', 'THE OLD ROAD', [75, 126], [500, 30], 16, '#BCAA8BFF'),
            label('loading_tip_title', 'Ride the valley', [76, 500], [850, 34], 24, '#EBD7AEFF'),
            label('loading_tip', 'WASD to ride. Hold Shift to gallop. Release movement to rein in.',
                  [76, 545], [1050, 34], 17, '#C7C9C7FF'),
            label('loading_stage', 'Preparing the journey', [76, 619], [850, 30], 15, '#BCAA8BFF'),
            {
                'id': 'loading_progress',
                'type': 'progress',
                'at': [76, 660],
                'size': [1128, 5],
                'value': 0,
                'color': '#D4B67CFF',
                'background_color': '#344047FF',
            },
            label('loading_error', '', [76, 585], [1128, 50], 15, '#EDAB91FF'),
        ],
    })
    write_json(project / 'scenes/loading.scene.json', {
        'format_version': 1,
        'id': 'scene://amber_pass/loading',
        'name': 'Amber Pass loading',
        'hud': '../hud/loading.hud.json',
        'entities': [
            {
                'id': 'loading_camera',
                'components': {
                    'Transform3D': {},
                    'Camera3D': {'clear_color': [0.027, 0.063, 0.094, 1]},
                },
            },
            {
                'id': 'loading_controller',
                'components': {
                    'LuaScript': {'module': 'script://scripts/loading.lua'},
                },
            },
        ],
    })
