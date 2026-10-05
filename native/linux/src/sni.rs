//! Top bar entry: a StatusNotifierItem on the session bus.
//!
//! GNOME Shell shows these through the AppIndicator extension (on by
//! default in Ubuntu); KDE and other desktops show them natively. The item
//! draws a status dot and a 6-bar CPU wave as its icon and publishes the
//! "CPU % RAM % ⚡ W ↓ Net" readout as the Ayatana label, which the
//! AppIndicator extension prints next to the icon. Clicking the item opens
//! or closes the flyout.

use std::sync::mpsc;
use std::time::{Duration, Instant};
use zbus::object_server::SignalEmitter;

pub enum TrayEvent {
    Activate,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TrayState {
    pub label: String,
    pub title: String,
    pub tooltip: String,
    pub wave: Vec<f64>,
    pub hog: bool,
}

type Pixmap = Vec<(i32, i32, Vec<u8>)>;

struct Item {
    st: TrayState,
    pix: Pixmap,
    tx: async_channel::Sender<TrayEvent>,
}

#[zbus::interface(name = "org.kde.StatusNotifierItem")]
impl Item {
    #[zbus(property)]
    fn category(&self) -> &str {
        "Hardware"
    }
    #[zbus(property)]
    fn id(&self) -> &str {
        "pulse"
    }
    #[zbus(property)]
    fn title(&self) -> String {
        self.st.title.clone()
    }
    #[zbus(property)]
    fn status(&self) -> &str {
        if self.st.hog { "NeedsAttention" } else { "Active" }
    }
    #[zbus(property)]
    fn icon_name(&self) -> &str {
        ""
    }
    #[zbus(property)]
    fn icon_pixmap(&self) -> Pixmap {
        self.pix.clone()
    }
    #[zbus(property)]
    fn attention_icon_pixmap(&self) -> Pixmap {
        self.pix.clone()
    }
    #[zbus(property)]
    fn tool_tip(&self) -> (String, Pixmap, String, String) {
        (String::new(), Vec::new(), self.st.title.clone(), format!("{}\n{}", self.st.label, self.st.tooltip))
    }
    #[zbus(property)]
    fn item_is_menu(&self) -> bool {
        false
    }
    #[zbus(property)]
    fn menu(&self) -> zbus::zvariant::OwnedObjectPath {
        zbus::zvariant::OwnedObjectPath::try_from("/NO_DBUSMENU").unwrap()
    }
    #[zbus(property, name = "XAyatanaLabel")]
    fn x_ayatana_label(&self) -> String {
        self.st.label.clone()
    }
    #[zbus(property, name = "XAyatanaLabelGuide")]
    fn x_ayatana_label_guide(&self) -> String {
        "CPU 100%  RAM 100%  ⚡00.0W  ↓000 KB/s".into()
    }
    fn activate(&self, _x: i32, _y: i32) {
        let _ = self.tx.try_send(TrayEvent::Activate);
    }
    fn secondary_activate(&self, _x: i32, _y: i32) {
        let _ = self.tx.try_send(TrayEvent::Activate);
    }
    fn context_menu(&self, _x: i32, _y: i32) {
        let _ = self.tx.try_send(TrayEvent::Activate);
    }
    fn scroll(&self, _delta: i32, _orientation: &str) {}

    #[zbus(signal)]
    async fn new_icon(e: &SignalEmitter<'_>) -> zbus::Result<()>;
    #[zbus(signal)]
    async fn new_tool_tip(e: &SignalEmitter<'_>) -> zbus::Result<()>;
    #[zbus(signal)]
    async fn new_title(e: &SignalEmitter<'_>) -> zbus::Result<()>;
    #[zbus(signal)]
    async fn new_status(e: &SignalEmitter<'_>, status: &str) -> zbus::Result<()>;
    #[zbus(signal, name = "XAyatanaNewLabel")]
    async fn x_ayatana_new_label(e: &SignalEmitter<'_>, label: &str, guide: &str) -> zbus::Result<()>;
}

pub struct Tray {
    tx: mpsc::Sender<TrayState>,
    last: std::cell::RefCell<TrayState>,
}

const PATH: &str = "/StatusNotifierItem";

impl Tray {
    /// Starts the D-Bus thread. Events come back on the GTK main loop.
    /// Returns None when there is no session bus.
    pub fn spawn(on_event: impl Fn(TrayEvent) + 'static) -> Option<Tray> {
        let (etx, erx) = async_channel::unbounded::<TrayEvent>();
        let (tx, rx) = mpsc::channel::<TrayState>();
        let conn = match zbus::blocking::connection::Builder::session()
            .and_then(|b| b.name(format!("org.kde.StatusNotifierItem-{}-1", std::process::id())))
            .and_then(|b| b.serve_at(PATH, Item { st: TrayState::default(), pix: Vec::new(), tx: etx }))
            .and_then(|b| b.build())
        {
            Ok(c) => c,
            Err(e) => {
                eprintln!("pulse: no top bar entry (session bus: {e}); use --show-flyout");
                return None;
            }
        };
        gtk::glib::spawn_future_local(async move {
            while let Ok(ev) = erx.recv().await {
                on_event(ev);
            }
        });
        std::thread::Builder::new()
            .name("pulse-sni".into())
            .spawn(move || run(conn, rx))
            .ok()?;
        Some(Tray { tx, last: Default::default() })
    }

    pub fn update(&self, st: TrayState) {
        if *self.last.borrow() != st {
            *self.last.borrow_mut() = st.clone();
            let _ = self.tx.send(st);
        }
    }
}

fn register(conn: &zbus::blocking::Connection) -> bool {
    let name = format!("org.kde.StatusNotifierItem-{}-1", std::process::id());
    let r = conn.call_method(
        Some("org.kde.StatusNotifierWatcher"),
        "/StatusNotifierWatcher",
        Some("org.kde.StatusNotifierWatcher"),
        "RegisterStatusNotifierItem",
        &(name.as_str(),),
    );
    if let Err(e) = &r {
        eprintln!("pulse: no StatusNotifierWatcher yet ({e}); will retry");
    }
    r.is_ok()
}

fn run(conn: zbus::blocking::Connection, rx: mpsc::Receiver<TrayState>) {
    let mut registered = register(&conn);
    let mut last_try = Instant::now();
    let iface = match conn.object_server().interface::<_, Item>(PATH) {
        Ok(i) => i,
        Err(e) => {
            eprintln!("pulse: tray interface: {e}");
            return;
        }
    };
    loop {
        let st = match rx.recv_timeout(Duration::from_secs(10)) {
            Ok(s) => Some(s),
            Err(mpsc::RecvTimeoutError::Timeout) => None,
            Err(mpsc::RecvTimeoutError::Disconnected) => return,
        };
        if !registered && last_try.elapsed() > Duration::from_secs(10) {
            registered = register(&conn);
            last_try = Instant::now();
        }
        let Some(st) = st else { continue };
        let pix = vec![
            (22, 22, crate::ui::draw::tray_pixmap(22, &st.wave, st.hog)),
            (44, 44, crate::ui::draw::tray_pixmap(44, &st.wave, st.hog)),
        ];
        let (label, status_changed) = {
            let mut it = iface.get_mut();
            let changed = it.st.hog != st.hog;
            it.st = st.clone();
            it.pix = pix;
            (st.label.clone(), changed)
        };
        let e = iface.signal_emitter();
        zbus::block_on(async {
            let _ = Item::new_icon(e).await;
            let _ = Item::new_tool_tip(e).await;
            let _ = Item::x_ayatana_new_label(e, &label, "CPU 100%  RAM 100%  ⚡00.0W  ↓000 KB/s").await;
            if status_changed {
                let _ = Item::new_status(e, if st.hog { "NeedsAttention" } else { "Active" }).await;
            }
        });
    }
}
