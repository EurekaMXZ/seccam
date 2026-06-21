use std::time::{SystemTime, UNIX_EPOCH};

use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        State,
    },
    response::Response,
    routing::get,
    Router,
};
use serde::Serialize;
use tokio::time::{self, Duration, MissedTickBehavior};

use crate::AppState;

pub fn status_router() -> Router<AppState> {
    Router::new().route("/ws/status", get(status_websocket))
}

async fn status_websocket(ws: WebSocketUpgrade, State(state): State<AppState>) -> Response {
    ws.on_upgrade(move |socket| handle_socket(socket, state))
}

async fn handle_socket(mut socket: WebSocket, state: AppState) {
    let mut ticker = time::interval(Duration::from_secs(1));
    ticker.set_missed_tick_behavior(MissedTickBehavior::Skip);
    let mut last_signature = String::new();

    loop {
        match build_frame(&state).await {
            Ok(frame) => {
                let signature = match serde_json::to_string(&(&frame.status, &frame.settings, &frame.events)) {
                    Ok(signature) => signature,
                    Err(err) => {
                        if send_error(&mut socket, format!("serialize websocket signature failed: {err}"))
                            .await
                            .is_err()
                        {
                            break;
                        }
                        return;
                    }
                };
                let payload = match serde_json::to_string(&frame) {
                    Ok(payload) => payload,
                    Err(err) => {
                        if send_error(&mut socket, format!("serialize websocket payload failed: {err}"))
                            .await
                            .is_err()
                        {
                            break;
                        }
                        return;
                    }
                };
                if signature != last_signature {
                    if socket.send(Message::Text(payload.clone().into())).await.is_err() {
                        break;
                    }
                    last_signature = signature;
                }
            }
            Err(err) => {
                if send_error(&mut socket, err.to_string()).await.is_err() {
                    break;
                }
                return;
            }
        }

        ticker.tick().await;
    }
}

async fn send_error(socket: &mut WebSocket, message: String) -> Result<(), ()> {
    let frame = WsErrorFrame {
        kind: "error",
        happened_ms: now_unix_ms(),
        error: message,
    };
    let payload = serde_json::to_string(&frame).map_err(|_| ())?;
    socket.send(Message::Text(payload.into())).await.map_err(|_| ())
}

async fn build_frame(state: &AppState) -> Result<WsStatusFrame, seccam_application::ApplicationError> {
    Ok(WsStatusFrame {
        kind: "snapshot",
        happened_ms: now_unix_ms(),
        status: state.services().status().await?,
        settings: state.services().get_settings().await?,
        events: state.services().list_events(32).await?,
    })
}

fn now_unix_ms() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

#[derive(Debug, Serialize)]
struct WsStatusFrame {
    kind: &'static str,
    happened_ms: u64,
    status: seccam_domain::LiveStatusSnapshot,
    settings: seccam_domain::RuntimeConfigSnapshot,
    events: Vec<seccam_domain::EventSummary>,
}

#[derive(Debug, Serialize)]
struct WsErrorFrame {
    kind: &'static str,
    happened_ms: u64,
    error: String,
}
