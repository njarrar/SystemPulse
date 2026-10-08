//! Top bar entry: a StatusNotifierItem on the session bus.
//!
//! GNOME Shell shows these through the AppIndicator extension (on by
//! default in Ubuntu); KDE and other desktops show them natively. The item
//! draws a status dot and a 6-bar CPU wave as its icon and publishes the
//! "CPU % RAM % ⚡ W ↓ Net" readout as the Ayatana label, which the
//! AppIndicator extension prints next to the icon. Clicking the item opens
//! or closes the flyout. Right-click shows a com.canonical.dbusmenu menu
//! (Open Pulse, Check for Updates…, Quit) in the app language.

use std::collections::HashMap;
use std::sync::mpsc;
use std::time::{Duration, Instant};
use zbus::object_server::SignalEmitter;
use zbus::zvariant::{OwnedValue, StructureBuilder, Type, Value};

pub enum TrayEvent {
    Activate,
    Open,
    CheckUpdates,
    Quit,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TrayState {
    pub label: String,
    pub title: String,
    pub tooltip: String,
    pub wave: Vec<f64>,
    pub hog: bool,
    /// Menu labels: open, check for updates, quit.
    pub menu: [String; 3],
    pub rtl: bool,
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
        zbus::zvariant::OwnedObjectPath::try_from(MENU_PATH).unwrap()
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
    /// Hosts draw the menu exported at `Menu`, so there is nothing to do.
    fn context_menu(&self, _x: i32, _y: i32) {}
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

// Menu item ids. 0 is the root; 2 and 4 are separators.
const ID_OPEN: i32 = 1;
const ID_CHECK: i32 = 3;
const ID_QUIT: i32 = 5;
const MENU_IDS: [i32; 5] = [ID_OPEN, 2, ID_CHECK, 4, ID_QUIT];

/// One dbusmenu layout node: (ia{sv}av).
#[derive(serde::Serialize, Type, Debug)]
struct Node {
    id: i32,
    props: HashMap<String, OwnedValue>,
    children: Vec<OwnedValue>,
}

fn owned<'a>(v: impl Into<Value<'a>>) -> OwnedValue {
    OwnedValue::try_from(v.into()).expect("no fds in menu values")
}

impl Node {
    fn into_value(self) -> OwnedValue {
        let s = StructureBuilder::new().add_field(self.id).add_field(self.props).add_field(self.children).build().expect("menu node");
        owned(s)
    }
}

struct Menu {
    labels: [String; 3],
    rtl: bool,
    revision: u32,
    tx: async_channel::Sender<TrayEvent>,
}

impl Menu {
    /// Properties of one item, limited to `names` when it is not empty.
    /// None for an unknown id.
    fn props(&self, id: i32, names: &[String]) -> Option<HashMap<String, OwnedValue>> {
        let mut p: Vec<(&str, OwnedValue)> = match id {
            0 => vec![("children-display", owned("submenu"))],
            2 | 4 => vec![("type", owned("separator"))],
            ID_OPEN | ID_CHECK | ID_QUIT => {
                let i = [ID_OPEN, ID_CHECK, ID_QUIT].iter().position(|x| *x == id).unwrap();
                vec![("label", owned(self.labels[i].as_str())), ("enabled", owned(true)), ("visible", owned(true))]
            }
            _ => return None,
        };
        if !names.is_empty() {
            p.retain(|(k, _)| names.iter().any(|n| n == k));
        }
        Some(p.into_iter().map(|(k, v)| (k.to_string(), v)).collect())
    }

    fn node(&self, id: i32, depth: i32, names: &[String]) -> Option<Node> {
        let props = self.props(id, names)?;
        let children = if id == 0 && depth != 0 {
            MENU_IDS.iter().filter_map(|c| self.node(*c, depth - 1, names)).map(Node::into_value).collect()
        } else {
            Vec::new()
        };
        Some(Node { id, props, children })
    }

    fn event_for(id: i32) -> Option<TrayEvent> {
        match id {
            ID_OPEN => Some(TrayEvent::Open),
            ID_CHECK => Some(TrayEvent::CheckUpdates),
            ID_QUIT => Some(TrayEvent::Quit),
            _ => None,
        }
    }

    fn known(id: i32) -> bool {
        id == 0 || MENU_IDS.contains(&id)
    }
}

#[zbus::interface(name = "com.canonical.dbusmenu")]
impl Menu {
    #[zbus(property)]
    fn version(&self) -> u32 {
        3
    }
    #[zbus(property)]
    fn text_direction(&self) -> &str {
        if self.rtl { "rtl" } else { "ltr" }
    }
    #[zbus(property)]
    fn status(&self) -> &str {
        "normal"
    }
    #[zbus(property)]
    fn icon_theme_path(&self) -> Vec<String> {
        Vec::new()
    }

    #[zbus(out_args("revision", "layout"))]
    fn get_layout(&self, parent_id: i32, recursion_depth: i32, property_names: Vec<String>) -> zbus::fdo::Result<(u32, Node)> {
        let node = self
            .node(parent_id, recursion_depth, &property_names)
            .ok_or_else(|| zbus::fdo::Error::InvalidArgs(format!("no menu item {parent_id}")))?;
        Ok((self.revision, node))
    }
    fn get_group_properties(&self, ids: Vec<i32>, property_names: Vec<String>) -> Vec<(i32, HashMap<String, OwnedValue>)> {
        let ids = if ids.is_empty() { std::iter::once(0).chain(MENU_IDS).collect() } else { ids };
        ids.into_iter().filter_map(|id| self.props(id, &property_names).map(|p| (id, p))).collect()
    }
    fn get_property(&self, id: i32, name: String) -> zbus::fdo::Result<OwnedValue> {
        self.props(id, std::slice::from_ref(&name))
            .and_then(|mut p| p.remove(&name))
            .ok_or_else(|| zbus::fdo::Error::InvalidArgs(format!("no property {name} on menu item {id}")))
    }
    fn event(&self, id: i32, event_id: &str, _data: Value<'_>, _timestamp: u32) -> zbus::fdo::Result<()> {
        if !Menu::known(id) {
            return Err(zbus::fdo::Error::InvalidArgs(format!("no menu item {id}")));
        }
        if event_id == "clicked" {
            if let Some(ev) = Menu::event_for(id) {
                let _ = self.tx.try_send(ev);
            }
        }
        Ok(())
    }
    fn event_group(&self, events: Vec<(i32, String, OwnedValue, u32)>) -> zbus::fdo::Result<Vec<i32>> {
        let errors: Vec<i32> = events.iter().filter(|e| !Menu::known(e.0)).map(|e| e.0).collect();
        if !events.is_empty() && errors.len() == events.len() {
            return Err(zbus::fdo::Error::InvalidArgs("no such menu items".into()));
        }
        for (id, event_id, _, _) in &events {
            if event_id == "clicked" {
                if let Some(ev) = Menu::event_for(*id) {
                    let _ = self.tx.try_send(ev);
                }
            }
        }
        Ok(errors)
    }
    fn about_to_show(&self, _id: i32) -> bool {
        false
    }
    #[zbus(out_args("updatesNeeded", "idErrors"))]
    fn about_to_show_group(&self, ids: Vec<i32>) -> (Vec<i32>, Vec<i32>) {
        (Vec::new(), ids.into_iter().filter(|id| !Menu::known(*id)).collect())
    }

    #[zbus(signal)]
    async fn items_properties_updated(
        e: &SignalEmitter<'_>,
        updated_props: Vec<(i32, HashMap<String, OwnedValue>)>,
        removed_props: Vec<(i32, Vec<String>)>,
    ) -> zbus::Result<()>;
    #[zbus(signal)]
    async fn layout_updated(e: &SignalEmitter<'_>, revision: u32, parent: i32) -> zbus::Result<()>;
    #[zbus(signal)]
    async fn item_activation_requested(e: &SignalEmitter<'_>, id: i32, timestamp: u32) -> zbus::Result<()>;
}

pub struct Tray {
    tx: mpsc::Sender<TrayState>,
    last: std::cell::RefCell<TrayState>,
}

const PATH: &str = "/StatusNotifierItem";
const MENU_PATH: &str = "/MenuBar";

impl Tray {
    /// Starts the D-Bus thread. Events come back on the GTK main loop.
    /// Returns None when there is no session bus.
    pub fn spawn(on_event: impl Fn(TrayEvent) + 'static) -> Option<Tray> {
        let (etx, erx) = async_channel::unbounded::<TrayEvent>();
        let (tx, rx) = mpsc::channel::<TrayState>();
        let conn = match zbus::blocking::connection::Builder::session()
            .and_then(|b| b.name(format!("org.kde.StatusNotifierItem-{}-1", std::process::id())))
            .and_then(|b| b.serve_at(PATH, Item { st: TrayState::default(), pix: Vec::new(), tx: etx.clone() }))
            .and_then(|b| b.serve_at(MENU_PATH, Menu { labels: Default::default(), rtl: false, revision: 1, tx: etx }))
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
    let (iface, menu) = match (conn.object_server().interface::<_, Item>(PATH), conn.object_server().interface::<_, Menu>(MENU_PATH)) {
        (Ok(i), Ok(m)) => (i, m),
        (Err(e), _) | (_, Err(e)) => {
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
        update_menu(&menu, &st);
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

/// Applies new labels or direction (a language switch) and tells hosts to
/// fetch the layout again.
fn update_menu(menu: &zbus::blocking::object_server::InterfaceRef<Menu>, st: &TrayState) {
    let (rev, dir_changed, updated) = {
        let mut m = menu.get_mut();
        if m.labels == st.menu && m.rtl == st.rtl {
            return;
        }
        let dir_changed = m.rtl != st.rtl;
        m.labels = st.menu.clone();
        m.rtl = st.rtl;
        m.revision += 1;
        let names = ["label".to_string()];
        let updated: Vec<_> = [ID_OPEN, ID_CHECK, ID_QUIT].iter().filter_map(|id| m.props(*id, &names).map(|p| (*id, p))).collect();
        (m.revision, dir_changed, updated)
    };
    let e = menu.signal_emitter();
    zbus::block_on(async {
        let _ = Menu::items_properties_updated(e, updated, Vec::new()).await;
        let _ = Menu::layout_updated(e, rev, 0).await;
        if dir_changed {
            let _ = menu.get().text_direction_changed(e).await;
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn menu() -> Menu {
        let (tx, _rx) = async_channel::unbounded();
        Menu { labels: ["Open Pulse".into(), "Check for Updates…".into(), "Quit".into()], rtl: false, revision: 1, tx }
    }

    fn label(n: &Node) -> Option<String> {
        n.props.get("label").and_then(|v| String::try_from(v.try_clone().unwrap()).ok())
    }

    #[test]
    fn layout_has_three_items_and_two_separators() {
        let m = menu();
        let root = m.node(0, -1, &[]).unwrap();
        assert_eq!(root.children.len(), 5);
        let kids: Vec<Node> = MENU_IDS.iter().map(|id| m.node(*id, 0, &[]).unwrap()).collect();
        let labels: Vec<Option<String>> = kids.iter().map(label).collect();
        assert_eq!(labels, vec![Some("Open Pulse".into()), None, Some("Check for Updates…".into()), None, Some("Quit".into())]);
        assert!(kids[1].props.contains_key("type") && kids[3].props.contains_key("type"));
        // the reply body has the dbusmenu GetLayout signature and serializes
        assert_eq!(<(u32, Node)>::SIGNATURE.to_string(), "(u(ia{sv}av))");
        let ctxt = zbus::zvariant::serialized::Context::new_dbus(zbus::zvariant::LE, 0);
        zbus::zvariant::to_bytes(ctxt, &(1u32, root)).unwrap();
    }

    #[test]
    fn property_filter_and_events() {
        let m = menu();
        let p = m.props(ID_QUIT, &["label".into()]).unwrap();
        assert_eq!(p.len(), 1);
        assert!(m.props(9, &[]).is_none());
        assert!(matches!(Menu::event_for(ID_OPEN), Some(TrayEvent::Open)));
        assert!(matches!(Menu::event_for(ID_CHECK), Some(TrayEvent::CheckUpdates)));
        assert!(matches!(Menu::event_for(ID_QUIT), Some(TrayEvent::Quit)));
        assert!(Menu::event_for(2).is_none());
    }
}
