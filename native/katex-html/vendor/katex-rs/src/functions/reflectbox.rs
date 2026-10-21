//! Mirrored content (`\reflectbox` and `\mathreflectbox`), matching KaTeX 0.19.

use crate::build_common::make_span;
use crate::define_function::{FunctionDefSpec, FunctionPropSpec};
use crate::dom_tree::HtmlDomNode;
use crate::mathml_tree::MathDomNode;
use crate::options::Options;
use crate::parser::parse_node::{AnyParseNode, NodeType, ParseNodeReflectbox};
use crate::types::{ArgType, Mode, ParseError, ParseErrorKind};
use crate::{ClassList, KatexContext, build_html, build_mathml};

/// Registers `\reflectbox` and `\mathreflectbox`.
pub fn define_reflectbox(ctx: &mut KatexContext) {
    ctx.define_function(FunctionDefSpec {
        node_type: Some(NodeType::Reflectbox),
        names: &["\\reflectbox"],
        props: FunctionPropSpec {
            num_args: 1,
            arg_types: Some(vec![ArgType::Hbox]),
            allowed_in_text: true,
            ..Default::default()
        },
        handler: Some(|context, args, _| {
            Ok(AnyParseNode::Reflectbox(ParseNodeReflectbox {
                mode: context.parser.mode,
                loc: context.loc(),
                body: Box::new(args[0].clone()),
            }))
        }),
        html_builder: Some(html_builder),
        mathml_builder: Some(mathml_builder),
    });
    ctx.define_function(FunctionDefSpec {
        node_type: Some(NodeType::Reflectbox),
        names: &["\\mathreflectbox"],
        props: FunctionPropSpec {
            num_args: 1,
            arg_types: Some(vec![ArgType::Mode(Mode::Math)]),
            allowed_in_text: true,
            ..Default::default()
        },
        handler: Some(|context, args, _| {
            Ok(AnyParseNode::Reflectbox(ParseNodeReflectbox {
                mode: context.parser.mode,
                loc: context.loc(),
                body: Box::new(args[0].clone()),
            }))
        }),
        html_builder: None,
        mathml_builder: None,
    });
}

fn html_builder(
    node: &AnyParseNode,
    options: &Options,
    ctx: &KatexContext,
) -> Result<HtmlDomNode, ParseError> {
    let AnyParseNode::Reflectbox(reflect) = node else {
        return Err(ParseError::new(ParseErrorKind::ExpectedNode {
            node: NodeType::Reflectbox,
        }));
    };
    let body = build_html::build_group(ctx, &reflect.body, options, None)?;
    Ok(make_span(
        ClassList::Const(&["mord", "reflectbox"]),
        vec![body],
        Some(options),
        None,
    )
    .into())
}

fn mathml_builder(
    node: &AnyParseNode,
    options: &Options,
    ctx: &KatexContext,
) -> Result<MathDomNode, ParseError> {
    let AnyParseNode::Reflectbox(reflect) = node else {
        return Err(ParseError::new(ParseErrorKind::ExpectedNode {
            node: NodeType::Reflectbox,
        }));
    };
    build_mathml::build_group(ctx, &reflect.body, options)
}
