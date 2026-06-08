import { RELEVANCE, EFFECT_BLURB, PARAM_DEFS } from "../ampParams";

const LABEL = Object.fromEntries(PARAM_DEFS.map((d) => [d.key, d.label]));

// LCD-style centerpiece: active effect + the parameters that matter for it.
export default function DisplayScreen({ params }) {
  const fx = params.effect;
  const keys = RELEVANCE[fx] || [];

  return (
    <div className="lcd">
      <div className="lcd-scan" aria-hidden="true" />
      <div className="lcd-row">
        <div className="lcd-effect">
          <span className="lcd-dot" />
          {fx}
        </div>
        <div className="lcd-blurb">{EFFECT_BLURB[fx]}</div>
      </div>
      <div className="lcd-values">
        {keys.length === 0 ? (
          <span className="lcd-pair"><i>bypass</i></span>
        ) : (
          keys.map((k) => (
            <span className="lcd-pair" key={k}>
              <i>{LABEL[k]}</i>
              <b>{params[k]}</b>
            </span>
          ))
        )}
      </div>
    </div>
  );
}
