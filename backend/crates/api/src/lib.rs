mod routes;

use std::sync::Arc;

use axum::Router;
use seccam_application::AppServices;
use tower_http::{cors::CorsLayer, trace::TraceLayer};

#[derive(Clone)]
pub struct AppState {
    services: Arc<AppServices>,
}

impl AppState {
    pub fn new(services: Arc<AppServices>) -> Self {
        Self { services }
    }

    pub fn services(&self) -> &Arc<AppServices> {
        &self.services
    }
}

pub fn build_router() -> Router<AppState> {
    Router::new()
        .merge(routes::health_routes())
        .merge(routes::status_ws_routes())
        .nest("/api/v1", routes::v1_routes())
        .layer(CorsLayer::permissive())
        .layer(TraceLayer::new_for_http())
}
