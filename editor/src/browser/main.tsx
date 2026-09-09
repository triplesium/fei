import { createRoot } from "react-dom/client";
import "dockview-react/dist/styles/dockview.css";
import { App } from "./app";
import "./styles.css";

createRoot(document.getElementById("root")!).render(<App />);
